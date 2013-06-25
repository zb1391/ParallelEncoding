/* encode_test2.c
 *   by:
 *   	Pradnya Pisal, Praveen Chekuri, Zac Brown
 *    
 *    	  Based off sample code provided with ffmpeg libraries
 *   
 *     	   This program opens any video format and compresses it using MPEG-1 to a .mpg file
 *     	    it does not save the audio however. 
 *    	     
 *        Use
 *   		build:
 *             	        mpicc -Wall -O2 -g   -c -o parallel.o parallel_final.c

 			mpicc parallel.o  -pthread -lavdevice -lavfilter -lavformat -lavcodec -ldl \
                        -lasound -lSDL -lpthread -lz -lrt -lswresample -lswscale -lavutil -lm    -o parallel

 *                     Run using (definitely works with avi files)
 *                     mpirun -np [num-procs] ./parallel
 *                     
			Does not check for errors with user input (doesnt check if user supplies at least 2 nodes
 *                
		The process of compressing video and audio into from one file is very complex
 *              You first have to demux the video file (separate the video/audio into two different files)
 *              Then you compress each one individually
 *              Then you mux the two compressed files back into one compressed file.
 *              I dont think we have time to create code to mux/demux a video file.
 *
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include "mpi.h"

/*Hard Coded to run with a particular test video ffNT.avi*/
#define VIDEO_BUFFER_SIZE 654*332
#define FFMPI_PACKET_SIZE (VIDEO_BUFFER_SIZE + 8*sizeof(int))
#define RUNTIME 21*60*1000
#define FRAME_TOTAL  30220

/*
 *  * This sends a single packet from the slave to the master
 *   * We want to run this in loop for each packet that the slave has
 *   */
static void mpi_sendPacket(AVPacket *pk){
	int size = pk->size + sizeof(*pk);
	char buf[size];
        int pos = 0;
	MPI_Pack(&pk->size, 1, MPI_INT, buf, size, &pos, MPI_COMM_WORLD);
	MPI_Pack(&pk->pts, 1, MPI_LONG_LONG, buf, size, &pos, MPI_COMM_WORLD);
	MPI_Pack(&pk->stream_index, 1, MPI_INT, buf, size, &pos, MPI_COMM_WORLD);
	MPI_Pack(&pk->flags, 1, MPI_INT, buf, size, &pos, MPI_COMM_WORLD);
	MPI_Pack(&pk->duration, 1, MPI_INT, buf, size, &pos, MPI_COMM_WORLD);
	MPI_Pack(pk->data, pk->size, MPI_CHAR, buf, size, &pos, MPI_COMM_WORLD);
	

	/*Then send the actual packet*/
	MPI_Send(buf, pos, MPI_PACKED, 0, 0, MPI_COMM_WORLD);
}


/* This takes an empty AVPacket as an input
 *    it then receives the pkt->data and pkt->size
 *       then it calls MPI_Unpack and places that information into the empty AVPacket*/
static void mpi_recvPacket(AVPacket* pk,int slave,MPI_Status *status){
	char buf[FFMPI_PACKET_SIZE];
	int size, pos=0;
	/*Then receive the actual packet*/
	/*printf("master is attempting to receive packet\n");*/
	MPI_Recv(buf,FFMPI_PACKET_SIZE,MPI_PACKED,slave,0,MPI_COMM_WORLD,status);
	/*printf("master received buffer\n");*/

	/*Unpack the size*/
	MPI_Unpack(buf,FFMPI_PACKET_SIZE,&pos,&size,1,MPI_INT,MPI_COMM_WORLD);
	/*printf("master has unpacked the size parameter\n");	*/
	
	/*Initialize Packet*/	
	av_init_packet(pk);
	av_new_packet(pk,size);
	
	/*Unpack the parameters of the AVPacket*/
    	MPI_Unpack(buf, FFMPI_PACKET_SIZE, &pos, &pk->pts,
	       1, MPI_LONG_LONG, MPI_COMM_WORLD);
        MPI_Unpack(buf, FFMPI_PACKET_SIZE, &pos, &pk->stream_index,
	       1, MPI_INT, MPI_COMM_WORLD);
        MPI_Unpack(buf, FFMPI_PACKET_SIZE, &pos, &pk->flags,
	       1, MPI_INT, MPI_COMM_WORLD);
        MPI_Unpack(buf, FFMPI_PACKET_SIZE, &pos, &pk->duration,
	       1, MPI_INT, MPI_COMM_WORLD);
        MPI_Unpack(buf, FFMPI_PACKET_SIZE, &pos, pk->data,
	       pk->size, MPI_CHAR, MPI_COMM_WORLD);

}

/*Use this to start decoding/encoding the video at a particular time*/
int seek_frame(int tsms, AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx,int videoStream){
	int64_t frame;
	frame = av_rescale(tsms,pFormatCtx->streams[videoStream]->time_base.den,
		pFormatCtx->streams[videoStream]->time_base.num);
	frame=frame/1000;

	if(avformat_seek_file(pFormatCtx,videoStream,0,frame,frame,AVSEEK_FLAG_FRAME)<0)
		return 0;

	avcodec_flush_buffers(pCodecCtx);
	return 1;
}



/*slave code*/
int slave(int start_time, int frame_count,int slaveid) {
  AVFormatContext *pFormatCtx;
  int             i, videoStream;
  AVCodecContext  *pCodecCtx;
  AVCodec         *pCodec;
  AVFrame         *pFrame; 
  AVFrame         *pFrameRGB;
  AVPacket        packet;
  int             frameFinished;
  int             numBytes;
  uint8_t         *buffer;
  AVPacket	  **packetList;


  struct SwsContext      *sws_ctx = NULL;


  /*Register all formats and codecs*/
  av_register_all();

  /*printf("registered all formats and codecs\n");*/

  /*Allocate an AVFormatContext.*/
  pFormatCtx = avformat_alloc_context();
  
  /* Open video file*/
  if(avformat_open_input(&pFormatCtx, "ffNT.avi", NULL, NULL)!=0)
    return -1; /* Couldn't open file*/
  /*printf("opened file\n");*/

  /*Retrieve stream information*/
  if(av_find_stream_info(pFormatCtx)<0)
    return -1; /*Couldn't find stream information*/
  /*printf("retrieved stream info\n");  */
  
  /* Find the first video stream*/
  videoStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++)
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
      videoStream=i;
      break;
    }
  if(videoStream==-1)
    return -1; /*Didn't find a video stream*/
  /*printf("found first video stream\n");*/

  /* Get a pointer to the codec context for the video stream*/
  pCodecCtx=pFormatCtx->streams[videoStream]->codec;
  /*printf("got a pointer to the video stream codec\n");*/

  /*Find the decoder for the video stream*/
  pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; /* Codec not found*/
  }
  /*printf("found the appropriate codec\n");*/

  /* Open codec*/
  if(avcodec_open(pCodecCtx, pCodec)<0)
    return -1; /* Could not open codec*/
  /*printf("opened the proper codec\n");*/

  /*Allocate video frame*/
  pFrame=avcodec_alloc_frame();
  /*printf("allocate video frame\n");*/
  
  /* Allocate an AVFrame structure*/
  pFrameRGB=avcodec_alloc_frame();
  if(pFrameRGB==NULL)
    return -1;
  /*printf("created pFrameRGB\n");*/
  
  /*Used for converting image to RGB*/
  /*This sets up the context for creating the rgb image. I need to figure this out*/
  sws_ctx =
    sws_getContext
    (
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );


  /* Determine required buffer size and allocate buffer*/
  numBytes=avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width,
                  pCodecCtx->height);
  buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
  
  /* Assign appropriate parts of buffer to image planes in pFrameRGB
   *    Note that pFrameRGB is an AVFrame, but AVFrame is a superset
   *       of AVPicture*/
  avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_YUV420P,
         pCodecCtx->width, pCodecCtx->height);
  
  /*printf("reading stream and saving frames\n");*/
  i=0;

/*****Initializing the Encoder Codec *********/
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int ret, x, y, got_output;

    AVPacket *pkt;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };


    /* find the mpeg1 video encoder */
    codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two 
 *  *      *I hard coded this resolution based off the video i was compressing
 *   *           * I dont know what happens if you use a different sized video*/
    c->width = 654;
    c->height = 352;
    /* frames per second */
    c->time_base= (AVRational){1,25};
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;
    c->pix_fmt = PIX_FMT_YUV420P;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }


/***** End of Initialization *********/

  /*pt is a component each frame we are compressing, its like the frame id in a way*/
  int pt = 0;



  MPI_Status *status;
  /*MPI_Recv the starting time and frame count*/
  

  /*This is where the slave will know at what frame to begin decoding/encoding*/
  seek_frame(start_time,pFormatCtx,pCodecCtx,videoStream);


   int id;
   /*stream video, get each frame one at a time, compress*/
   printf("Starting the encoding process.\n");
  while(av_read_frame(pFormatCtx, &packet)>=0 && i<frame_count) {

    	/* Is this a packet from the video stream?*/
    	if(packet.stream_index==videoStream) {
      		/* Decode video frame*/
      		avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
		
	        /* Once we have a frame*/
      		if(frameFinished) {

			pkt = (AVPacket*)malloc(sizeof(AVPacket));
				av_init_packet(pkt);
				pkt->data = NULL;    
				pkt->size = 0;
				fflush(stdout);
		    /*leave this as is. deleting sws_scale breaks it for some reason*/
		    /* Convert the image from its native format to RGB*/
			/*This method converts the image to the right picture, i need to figure this out*/
			sws_scale
			(sws_ctx,
			    (uint8_t const * const *)pFrame->data,
			    pFrame->linesize,
			    0,
			    pCodecCtx->height,
			    pFrameRGB->data,
			    pFrameRGB->linesize
			);
			/*This is all stuff i added for encoding*/
			/*Encode the frame*/
			ret = avcodec_encode_video2(c, pkt, pFrameRGB, &got_output);
			if (ret < 0) {
			    fprintf(stderr, "Error encoding frame\n");
			    exit(1);
			}
			/*Once the packet is finished*/
			if (got_output) {
 			    /*printf("Slave%d is sending packet %d to packetList of packetsize:%d\n",slaveid,i,pkt->size);*/
			    mpi_sendPacket(pkt);
			}


      		}
     		i++;
    	}
    
	/* Free the packet that was allocated by av_read_frame */
	av_free_packet(&packet);
  }
printf("Slave %d finished encoding.\n",slaveid);



    avcodec_close(c);
    av_free(c);
    printf("\n");



  /* Free the RGB image*/
  av_free(buffer);
  av_free(pFrameRGB);
  
  /* Free the YUV frame */
  av_free(pFrame);
  
  /* Close the codec*/
  avcodec_close(pCodecCtx);
  
  /* Close the video file*/
  av_close_input_file(pFormatCtx);
  printf("finished\n");
  
  return 0;
}




int main(int argc, char** argv){
  int rank;
  int num_procs;
  MPI_Status status;

  /*Initialize MPI*/
  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&num_procs);

  /*number of slaves*/
  int slave_num = num_procs-1;

 /*number of frames each slave computes*/
  int frames = FRAME_TOTAL/slave_num;

  /*master*/
  if(rank==0){
	/*master needs to send each slave the proper starting time in milliseconds to each slave and number of frames to compute*/
	int i,j;
	int buf[2];
	buf[1]=frames;
	buf[0]=1;
  	printf("Each slave computes %d frames.\n",frames);

	/*Send the slaves the starting times/frame numbers*/
	for(i=1;i<=slave_num;i++){
		MPI_Send(buf,2,MPI_INT,i,0,MPI_COMM_WORLD);
    		printf("Sending slave %d [%d,%d]\n",i, buf[0], buf[1]);
		buf[0] += RUNTIME/slave_num;
	}

   	 printf("finished sending frames to slaves\n");

	/*open the file we are writing to*/
      	FILE *f;
	f = fopen("test.mpg", "wb");
	if (!f) {
		fprintf(stderr, "Could not open test.mpg\n");
		exit(1);
	}


	AVPacket* packet;
	/*Receive an AVPacket from slaves one at a time
	 *the packets must be received in order so they can be written in order
	 */	
	for( j=1; j<=slave_num; j++)	
		for(i=2;i<frames;i++){
			packet = (AVPacket*)malloc(sizeof(AVPacket));
			mpi_recvPacket(packet,j,&status);
			fwrite(packet->data, 1, packet->size, f);
			av_free_packet(packet);
			/*printf("Master has finished writing packet %d from slave %d\n",i,j);*/
		}
	

	printf("finished receiving all frames\nwriting packets to file\n");


	uint8_t endcode[] = { 0, 0, 1, 0xb7 };
	/* add sequence end code to have a real mpeg file */
	fwrite(endcode, 1, sizeof(endcode), f);
	fclose(f);

  }
  /*Slaves*/
  else{
    	int start_time, frame_count;
  	int buffer2[2];
	/*Receive the starting time and frame numbers from master*/
	MPI_Recv(buffer2,2,MPI_INT,0,0,MPI_COMM_WORLD,&status);
  	start_time=buffer2[0];
  	frame_count=buffer2[1];
  
	/*begin the decoding/encoding process*/
 	printf("starting at time %d and calculating %d frames\n",start_time,frame_count);
	slave(start_time, frame_count,rank);
  }
  MPI_Finalize();
  return 0;


}

