/*****************************************************************************
* Copyright (C) 2017-2020 Hanson Yu  All rights reserved.
------------------------------------------------------------------------------
* File Module       : 	FFmpegAndSDL.cpp
* Description       : 	FFmpegAndSDL Demo


* Created           : 	2017.09.21.
* Author            : 	Yu Weifeng
* Function List     : 	
* Last Modified     : 	
* History           : 	
* Modify Date	  Version		 Author 		  Modification
* -----------------------------------------------
* 2017/09/21	  V1.0.0		 Yu Weifeng 	  Created
******************************************************************************/

#include "stdafx.h"
#include <stdio.h>


/*解决错误：
LNK2019	无法解析的外部符号 __imp__fprintf，该符号在函数 _ShowError 中被引用

原因：
……这是链接库问题
就是工程里面没有添加那两个函数需要的库，#progma这个是代码链接库
第二句是vs2015兼容的问题。
lib库的vs编译版本 和 工程的vs开发版本 不一致。
导出函数定义变了。所以要人为加一个函数导出。
*/
#pragma comment(lib, "legacy_stdio_definitions.lib")
extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }

/*
__STDC_LIMIT_MACROS and __STDC_CONSTANT_MACROS are a workaround to allow C++ programs to use stdint.h
macros specified in the C99 standard that aren't in the C++ standard. The macros, such as UINT8_MAX, INT64_MIN,
and INT32_C() may be defined already in C++ applications in other ways. To allow the user to decide
if they want the macros defined as C99 does, many implementations require that __STDC_LIMIT_MACROS
and __STDC_CONSTANT_MACROS be defined before stdint.h is included.

This isn't part of the C++ standard, but it has been adopted by more than one implementation.
*/
#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
};


//Refresh Event 自定义事件
#define PLAY_REFRESH_EVENT       (SDL_USEREVENT + 1)//自定义刷新图像(播放)事件
#define PLAY_BREAK_EVENT         (SDL_USEREVENT + 2) //自定义退出播放事件


static int g_iThreadExitFlag = 0;
/*****************************************************************************
-Fuction        : RefreshPlayThread
-Description    : RefreshPlayThread
-Input          : 
-Output         : 
-Return         : 
* Modify Date	  Version		 Author 		  Modification
* -----------------------------------------------
* 2017/09/21	  V1.0.0		 Yu Weifeng 	  Created
******************************************************************************/
int RefreshPlayThread(void *opaque) 
{
	g_iThreadExitFlag = 0;
    SDL_Event tEvent={0};
    
	while (!g_iThreadExitFlag) 
	{
		tEvent.type = PLAY_REFRESH_EVENT;
		SDL_PushEvent(&tEvent);//发送事件给其他线程
		SDL_Delay(20);//延时函数 填40的时候，视频会有种卡的感觉
	}
	//Break
	g_iThreadExitFlag = 0;
	tEvent.type = PLAY_BREAK_EVENT;
	SDL_PushEvent(&tEvent);//发送事件给其他线程 发送一个事件

	return 0;
}

/*****************************************************************************
-Fuction        : main
-Description    : main
-Input          : 
-Output         : 
-Return         : 
* Modify Date	  Version		 Author 		  Modification
* -----------------------------------------------
* 2017/09/21	  V1.0.0		 Yu Weifeng 	  Created
******************************************************************************/
int main(int argc, char* argv[])
{
    /*------------FFmpeg----------------*/
	const char *strFilePath = "屌丝男士.mov";
	AVFormatContext	*ptFormatContext = NULL;//封装格式上下文，内部包含所有的视频信息
	int				i = 0; 
	int             iVideoindex=0;//纯视频信息在音视频流中的位置，也就是指向音视频流数组中的视频元素
	AVCodecContext	*ptCodecContext;//编码器相关信息上下文，内部包含编码器相关的信息，指向AVFormatContext中的streams成员中的codec成员
	AVCodec			*ptCodec;//编码器，使用函数avcodec_find_decoder或者，该函数需要的id参数，来自于ptCodecContext中的codec_id成员
	AVFrame	        *ptFrame=NULL;//存储一帧解码后像素（采样）数据
	AVFrame	        *ptFrameAfterScale=NULL;//存储(解码数据)转换后的像素（采样）数据
	unsigned char   *pucFrameAfterScaleBuf=NULL;//用于存储ptFrameAfterScale中的像素（采样）缓冲数据
	AVPacket        *ptPacket=NULL;//存储一帧压缩编码数据
	int             iRet =0;
	int             iGotPicture=0;//解码函数的返回参数，got_picture_ptr Zero if no frame could be decompressed, otherwise, it is nonzero

	/*------------SDL----------------*/
	int iScreenWidth=0, iScreenHeight=0;//视频的宽和高，指向ptCodecContext中的宽和高
	SDL_Window *ptSdlWindow=NULL;//用于sdl显示视频的窗口(用于显示的屏幕)
	SDL_Renderer* ptSdlRenderer=NULL;//sdl渲染器，把纹理数据画(渲染)到window上
	SDL_Texture* ptSdlTexture=NULL;//sdl纹理数据,用于存放像素（采样）数据，然后给渲染器
	SDL_Rect tSdlRect ={0};//正方形矩形结构，存了矩形的坐标，长宽，以便确定纹理数据画在哪个位置，确定位置用，比如画在左上角就用这个来确定。被渲染器调用
	SDL_Thread *ptVideoControlTID=NULL;//sdl线程id,线程的句柄
	SDL_Event tSdlEvent = {0};//sdl事件,代表一个事件

    /*------------像素数据处理----------------*/
	struct SwsContext *ptImgConvertInfo;//图像转换(上下文)信息，图像转换函数sws_scale需要的参数，由sws_getContext函数赋值



    /*------------FFmpeg----------------*/
	av_register_all();//注册FFmpeg所有组件
	avformat_network_init();//初始化网络组件
	
	ptFormatContext = avformat_alloc_context();//分配空间给ptFormatContext
	if (avformat_open_input(&ptFormatContext, strFilePath, NULL, NULL) != 0) 
	{//打开输入视频文件
		printf("Couldn't open input stream.\n");
		return -1;
	}
	if (avformat_find_stream_info(ptFormatContext, NULL)<0) 
	{//获取视频文件信息
		printf("Couldn't find stream information.\n");
		return -1;
	}
	//获取编码器相关信息上下文，并赋值给ptCodecContext
	iVideoindex = -1;
	for (i = 0; i<ptFormatContext->nb_streams; i++)
	{
		if (ptFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) 
		{
			iVideoindex = i;
			break;
		}
	}
	if (iVideoindex == -1) 
	{
		printf("Didn't find a video stream.\n");
		return -1;
	}
	ptCodecContext = ptFormatContext->streams[iVideoindex]->codec;
	
	ptCodec = avcodec_find_decoder(ptCodecContext->codec_id);//查找解码器
	if (ptCodec == NULL) 
	{
		printf("Codec not found.\n");
		return -1;
	}
	if (avcodec_open2(ptCodecContext, ptCodec, NULL)<0) 
	{//打开解码器
		printf("Could not open codec.\n");
		return -1;
	}
	
	ptPacket = (AVPacket *)av_malloc(sizeof(AVPacket));//分配保存解码前数据的空间
	ptFrame = av_frame_alloc();//分配结构体空间，结构体内部的指针指向的数据暂未分配,用于保存图像转换前的像素数据
	
	/*------------像素数据处理----------------*/
	ptFrameAfterScale = av_frame_alloc();//分配结构体空间，结构体内部的指针指向的数据暂未分配,用于保存图像转换后的像素数据
	pucFrameAfterScaleBuf = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, ptCodecContext->width, ptCodecContext->height));//分配保存数据的空间
     /*int avpicture_fill(AVPicture *picture, uint8_t *ptr,int pix_fmt, int width, int height);
    这个函数的使用本质上是为已经分配的空间的结构体(AVPicture *)ptFrame挂上一段用于保存数据的空间，
    这个结构体中有一个指针数组data[AV_NUM_DATA_POINTERS]，挂在这个数组里。一般我们这么使用：
    1） pFrameRGB=avcodec_alloc_frame();
    2） numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width,pCodecCtx->height);
        buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));
    3） avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24,pCodecCtx->width, pCodecCtx->height);
    以上就是为pFrameRGB挂上buffer。这个buffer是用于存缓冲数据的。
    ptFrame为什么不用fill空间。主要是下面这句：
    avcodec_decode_video(pCodecCtx, pFrame, &frameFinished,packet.data, packet.size);
    很可能是ptFrame已经挂上了packet.data，所以就不用fill了。*/
	avpicture_fill((AVPicture *)ptFrameAfterScale, pucFrameAfterScaleBuf, PIX_FMT_YUV420P, ptCodecContext->width, ptCodecContext->height);	
    //sws开头的函数用于处理像素(采样)数据
	ptImgConvertInfo = sws_getContext(ptCodecContext->width, ptCodecContext->height, ptCodecContext->pix_fmt,
		ptCodecContext->width, ptCodecContext->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);//获取图像转换(上下文)信息

    /*------------SDL----------------*/
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) 
	{//初始化SDL系统
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	//SDL 2.0 Support for multiple windows
	iScreenWidth = ptCodecContext->width;
	iScreenHeight = ptCodecContext->height;
	ptSdlWindow = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		iScreenWidth, iScreenHeight, SDL_WINDOW_OPENGL);//创建窗口SDL_Window

	if (!ptSdlWindow) 
	{
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	ptSdlRenderer = SDL_CreateRenderer(ptSdlWindow, -1, 0);//创建渲染器SDL_Renderer
	//IYUV: Y + U + V  (3 planes)
	//YV12: Y + V + U  (3 planes)
	//创建纹理SDL_Texture
	ptSdlTexture = SDL_CreateTexture(ptSdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, ptCodecContext->width, ptCodecContext->height);

	tSdlRect.x = 0;//x y值是左上角为圆点开始的坐标值，调整x y值以及w h值，就可以实现在窗口的指定位置显示，没有画面的地方为黑框
	tSdlRect.y = 0;//当x y等于0，w h等于窗口的宽高时即为全屏显示，此时调整宽高大小，只需调整窗口大小即可
	tSdlRect.w = iScreenWidth;
	tSdlRect.h = iScreenHeight;

	ptVideoControlTID = SDL_CreateThread(RefreshPlayThread, NULL, NULL);//创建一个线程
	
	while (1) 
	{//Event Loop		
		SDL_WaitEvent(&tSdlEvent);//Wait，等待其他线程过来的事件
		if (tSdlEvent.type == PLAY_REFRESH_EVENT) //自定义刷新图像(播放)事件
		{
			/*------------FFmpeg----------------*/
			if (av_read_frame(ptFormatContext, ptPacket) >= 0) //从输入文件读取一帧压缩数据
			{
				if (ptPacket->stream_index == iVideoindex) 
				{
					iRet = avcodec_decode_video2(ptCodecContext, ptFrame, &iGotPicture, ptPacket);//解码一帧压缩数据
					if (iRet < 0) 
					{
						printf("Decode Error.\n");
						return -1;
					}
					if (iGotPicture) 
				    {
				        //图像转换，sws_scale()函数需要用到的转换信息，即第一个参数，是由sws_getContext函数获得的
						sws_scale(ptImgConvertInfo, (const uint8_t* const*)ptFrame->data, ptFrame->linesize, 0, ptCodecContext->height, ptFrameAfterScale->data, ptFrameAfterScale->linesize);

						/*------------SDL----------------*/
						SDL_UpdateTexture(ptSdlTexture, NULL, ptFrameAfterScale->data[0], ptFrameAfterScale->linesize[0]);//设置(更新)纹理的数据
						SDL_RenderClear(ptSdlRenderer);//先清除渲染器里的数据
						//SDL_RenderCopy( ptSdlRenderer, ptSdlTexture, &tSdlRect, &tSdlRect );  //将纹理的数据拷贝给渲染器
						SDL_RenderCopy(ptSdlRenderer, ptSdlTexture, NULL, NULL);//将纹理的数据拷贝给渲染器
						SDL_RenderPresent(ptSdlRenderer);//显示
					}
				}
				av_free_packet(ptPacket);//释放空间
			}
			else 
			{				
				g_iThreadExitFlag = 1;//Exit Thread
			}
		}
		else if (tSdlEvent.type == SDL_QUIT) //也是SDL自带的事件，当点击窗口的×时触发//SDL_WINDOWENVENT sdl系统自带的事件，当拉伸窗口的时候会触发
		{
			g_iThreadExitFlag = 1;
		}
		else if (tSdlEvent.type == PLAY_BREAK_EVENT) //自定义退出播放事件
		{
			break;
		}

	}
	
	/*------------像素数据处理----------------*/
	sws_freeContext(ptImgConvertInfo);//释放空间
	
    /*------------SDL----------------*/
	SDL_Quit();//退出SDL系统

    /*------------FFmpeg----------------*/
	av_frame_free(&ptFrameAfterScale);//释放空间
	av_frame_free(&ptFrame);//释放空间
	avcodec_close(ptCodecContext);//关闭解码器
	avformat_close_input(&ptFormatContext);//关闭输入视频文件

	return 0;
}


