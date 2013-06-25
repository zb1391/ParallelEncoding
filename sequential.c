/* encode_test2.c
 by:
	Pradnya Pisal, Praveen Chekuri, Zac Brown
 
 Based off sample code provided with ffmpeg libraries

 This program opens any video format and compresses it using MPEG-1 to a .mpg file
 it does not save the audio however. 
 
Use

 build:
 gcc -I/usr/local/include -L/usr/local/lib encode_test2.c -lavformat -lavcodec -lswscale -lz -o test

 Run using (definitely works with avi files)
./test [PATH to video file]
./test $HOME/Desktop/himym/"Season 6"/"How I Met Your Mother - 601 - Big Days.avi"

 right now it also writes 3 frames from the video to image files because it was a test I was doing

*****
The process of compressing video and audio into from one file is very complex
You first have to demux the video file (separate the video/audio into two different files)
Then you compress each one individually
Then you mux the two compressed files back into one compressed file.
I dont think we have time to create code to mux/demux a video file.

There is source code given with the ffmpeg examples to demux/mux a video file but I dont know if we have time
to figure out how these work, and apply them to our project. It may be better to just do video compression only
*****
*/
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <stdio.h>

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
  FILE *pFile;
  char szFilename[32];
  int  y;
  printf("Saving frame %d, height is %d, width is %d\n",iFrame,width,height);
  /* Open file*/
  sprintf(szFilename, "frame%d.ppm", iFrame);
  pFile=fopen(szFilename, "wb");
  if(pFile==NULL)
    return;
  
  /* Write header*/
  fprintf(pFile, "P6\n%d %d\n255\n", width, height);
  
  /*Write pixel data*/
  for(y=0; y<height; y++)
    fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
  
  /* Close file*/
  fclose(pFile);
}

int main(int argc, char *argv[]) {
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
  struct SwsContext      *sws_ctx = NULL;

  if(argc < 2) {
    printf("Please provide a movie file\n");
    return -1;
  }
  /*Register all formats and codecs*/
  av_register_all();

  printf("registered all formats and codecs\n");

  /*Allocate an AVFormatContext.*/
  pFormatCtx = avformat_alloc_context();
  
  /* Open video file*/
  if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)!=0)
    return -1; /* Couldn't open file*/
  printf("opened file\n");

  /*Retrieve stream information*/
  if(av_find_stream_info(pFormatCtx)<0)
    return -1; /*Couldn't find stream information*/
  printf("retrieved stream info\n");  
  
  /* Find the first video stream*/
  videoStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++)
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
      videoStream=i;
      break;
    }
  if(videoStream==-1)
    return -1; /*Didn't find a video stream*/
  printf("found first video stream\n");

  /* Get a pointer to the codec context for the video stream*/
  pCodecCtx=pFormatCtx->streams[videoStream]->codec;
  printf("got a pointer to the video stream codec\n");

  /*Find the decoder for the video stream*/
  pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
  if(pCodec==NULL) {
    fprintf(stderr, "Unsupported codec!\n");
    return -1; /* Codec not found*/
  }
  printf("found the appropriate codec\n");

  /* Open codec*/
  if(avcodec_open(pCodecCtx, pCodec)<0)
    return -1; // Could not open codec
  printf("opened the proper codec\n");

  /*Allocate video frame*/
  pFrame=avcodec_alloc_frame();
  printf("allocate video frame\n");
  
  /* Allocate an AVFrame structure*/
  pFrameRGB=avcodec_alloc_frame();
  if(pFrameRGB==NULL)
    return -1;
  printf("created pFrameRGB\n");
  
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
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );


  /* Determine required buffer size and allocate buffer*/
  numBytes=avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width,
                  pCodecCtx->height);
  buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
  
  /* Assign appropriate parts of buffer to image planes in pFrameRGB
   Note that pFrameRGB is an AVFrame, but AVFrame is a superset
   of AVPicture*/
  avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_YUV420P,
         pCodecCtx->width, pCodecCtx->height);
  
  printf("reading stream and saving frames\n");
  i=0;

/*****Initializing the Encoder Codec *********/
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int ret, x, y, got_output;
    FILE *f;
    AVPacket pkt;
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
     *I hard coded this resolution based off the video i was compressing
     * I dont know what happens if you use a different sized video*/
    c->width = 654;
    c->height = 352;
    /* frames per second */
    c->time_base= (AVRational){1,25};
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    f = fopen("test.mpg", "wb");
    if (!f) {
        fprintf(stderr, "Could not open test.mpg\n");
        exit(1);
    }
/***** End of Initialization *********/

  /*pt is a component each frame we are compressing, its like the frame id in a way*/
  int pt = 0;

  /*stream video, get each frame one at a time, compress*/
  while(av_read_frame(pFormatCtx, &packet)>=0) {

	/*Need to initialize packet for the call to encoding method below
         *All packet data will be taken care of by the encoding method*/
        av_init_packet(&pkt);
        pkt.data = NULL;    
        pkt.size = 0;
	fflush(stdout);

    	/* Is this a packet from the video stream?*/
    	if(packet.stream_index==videoStream) {
      		/* Decode video frame*/
      		avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
      
	        /* Once we have a frame*/
      		if(frameFinished) {
		    /*I dont think I need this anymore because we want the frames in YUV format, not RGB*/
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
			/*Frame->pts is like the frame id*/ 
	    		pFrameRGB->pts = pt;
			pt++;
			/*Encode the frame*/
			ret = avcodec_encode_video2(c, &pkt, pFrameRGB, &got_output);
			if (ret < 0) {
			    fprintf(stderr, "Error encoding frame\n");
			    exit(1);
			}
			/*I dont fully understand the encode_video method or what got_output does yet*/
			if (got_output) {
			    /*printf("Write frame %3d (size=%5d)\n", i, pkt.size);*/
			    fwrite(pkt.data, 1, pkt.size, f);
			    av_free_packet(&pkt);
			}
			/*End of stuff I added*/

		        /* Save the frame to disk*/
    			if(i>9000 && i <9004)
			      SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height,i);
      		}
     		i++;
    	}
    
	/* Free the packet that was allocated by av_read_frame */
	av_free_packet(&packet);
  }
  printf("i is %d at the end of loop\n",i);

    /*in the example code this loop is ran after the encoding loop has finished*/
    /* get the delayed frames */
    /*while got_output is 1, increment i*/
    for (got_output = 1; got_output; i++) {
        fflush(stdout);

        ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf(stderr, "Error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            fwrite(pkt.data, 1, pkt.size, f);
            av_free_packet(&pkt);
        }
    }

    /* add sequence end code to have a real mpeg file */
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

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
  
  return 0;
}
