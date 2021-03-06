
// SDLVideoDlg.cpp: 实现文件
//

#include "stdafx.h"
#include "SDLVideo.h"
#include "SDLVideoDlg.h"
#include "afxdialogex.h"
#include "DS_AudioVideoDevices.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//Refresh  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1) 

//显示本地的视频，且推流到服务器
int push_local_av_thread(void *opaque)
{
	CSDLVideoDlg* pdlg = (CSDLVideoDlg*)opaque;
	int ret = 0;
	int dec_got_frame = 0;
	int enc_got_frame = 0;
	struct SwsContext *img_convert_ctx;
	int video_stream_idx = pdlg->m_videoindex;
	AVFormatContext *ifmt_ctx = pdlg->m_pFormatCtx;
	AVCodecContext  *pCodecCtx = pdlg->m_pCodecCtx;
	AVCodecContext  *pEncodecCtx;
	AVCodec         *pCodec = pdlg->m_pCodec;
	AVCodec         *pEncodec;
	AVStream* video_st;
	AVFormatContext *ofmt_ctx;
	int framecnt = 0;
	AVRational time_base_q = { 1, AV_TIME_BASE };
	int aud_next_pts = 0;
	int vid_next_pts = 0;
	int64_t start_time = av_gettime();
	const char* out_path = "rtmp://47.52.175.85:1935/myapp/test";
	//const char* out_path = "rtmp://send1.douyu.com/live/4226085rxbqUTlqI?wsSecret=221b7f60ecae7f4507e92a2ba2b8eeb0&wsTime=5a707ce5&wsSeek=off";

	//output initialize
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_path);
	//output video encoder initialize
	pEncodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!pEncodec)
	{
		return -1;
	}
	pEncodecCtx = avcodec_alloc_context3(pEncodec);
	pEncodecCtx->pix_fmt = PIX_FMT_YUV420P;
	pEncodecCtx->width = ifmt_ctx->streams[video_stream_idx]->codec->width;
	pEncodecCtx->height = ifmt_ctx->streams[video_stream_idx]->codec->height;
	pEncodecCtx->time_base.num = 1;
	pEncodecCtx->time_base.den = 25;
	pEncodecCtx->bit_rate = 400000;//300000;
	pEncodecCtx->gop_size = 250;//10;
	pEncodecCtx->max_qdiff = 4;
	pEncodecCtx->qcompress = 0.6;

	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		pEncodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

	pEncodecCtx->qmin = 10;
	pEncodecCtx->qmax = 51;
	//Optional Param
	pEncodecCtx->max_b_frames = 0;
	// Set H264 preset and tune
	AVDictionary *param = 0;
	// 通过--preset的参数调节编码速度和质量的平衡。
	//av_dict_set(&param, "preset", "superfast", 0);
	av_dict_set(&param, "preset", "slow", 0);
	// 通过--tune的参数值指定片子的类型，是和视觉优化的参数，或有特别的情况。
	// zerolatency: 零延迟，用在需要非常低的延迟的情况下，比如电视电话会议的编码
	av_dict_set(&param, "tune", "zerolatency", 0);
	av_dict_set(&param, "bufsize", "0", 0);
	if (avcodec_open2(pEncodecCtx, pEncodec, &param) < 0)
	{
		AfxMessageBox(L"Failed to open output video encoder!");
		return -1;
	}

	//创建一个输出stream
	video_st = avformat_new_stream(ofmt_ctx, pEncodec);
	if (video_st == NULL) {
		return -1;
	}
	video_st->time_base.num = 1;
	video_st->time_base.den = 25;
	video_st->codec = pEncodecCtx;

	//Open output URL,set before avformat_write_header() for muxing
	if (avio_open(&ofmt_ctx->pb, out_path, AVIO_FLAG_READ_WRITE) < 0)
	{
		AfxMessageBox(L"Failed to open output file!");
		return -1;
	}

	//Write File Header
	avformat_write_header(ofmt_ctx, NULL);

	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	int screen_w = pdlg->m_pCodecCtx->width;
	int screen_h = pdlg->m_pCodecCtx->height;
	sdlRenderer = SDL_CreateRenderer(pdlg->m_screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);
	AVFrame *pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, screen_w, screen_h));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, screen_w, screen_h);

	img_convert_ctx = sws_getContext(screen_w, screen_h, pdlg->m_pCodecCtx->pix_fmt, pdlg->m_pCodecCtx->width, pdlg->m_pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	while (1)
	{
		if (pdlg->exit_push_thread == 1) break;
		AVPacket *dec_pkt = (AVPacket *)av_malloc(sizeof(AVPacket));
		AVPacket enc_pkt;

		if ((ret = av_read_frame(ifmt_ctx, dec_pkt)) >= 0)
		{
			AVFrame *pframe = av_frame_alloc();
			
			if (!pframe)
			{
				ret = AVERROR(ENOMEM);
				return ret;
			}
			ret = avcodec_decode_video2(pdlg->m_pCodecCtx, pframe, &dec_got_frame, dec_pkt);
			if (ret < 0)
			{
				av_frame_free(&pframe);
				break;
			}

			if (dec_got_frame)
			{
				sws_scale(img_convert_ctx, (const uint8_t* const*)pframe->data, pframe->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);

				av_free_packet(dec_pkt);
				//SDL_Delay(40);
                av_frame_free(&pframe);

				enc_pkt.data = NULL;
				enc_pkt.size = 0;
				av_init_packet(&enc_pkt);
				ret = avcodec_encode_video2(pEncodecCtx, &enc_pkt, pFrameYUV, &enc_got_frame);
				if (ret < 0)
				{
					AfxMessageBox(L"encode failed.");
					break;
				}
#if 1			
				if (enc_got_frame == 1)
				{
					framecnt++;
					enc_pkt.stream_index = video_st->index;
					
					//Write PTS
					AVRational time_base = ofmt_ctx->streams[0]->time_base;//{ 1, 1000 };
					AVRational r_framerate1 = ifmt_ctx->streams[video_stream_idx]->r_frame_rate;//{ 50, 2 }; 
					char text[128] = {0};
					sprintf(text, "time_base is %d, fps is %d", time_base.num, r_framerate1.num);
					//AfxMessageBox(LPCTSTR(text));
					int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳
					enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
					enc_pkt.dts = enc_pkt.pts;
					enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);
					enc_pkt.pos = -1;
					vid_next_pts = framecnt*calc_duration; //general timebase
					int64_t pts_time = av_rescale_q(enc_pkt.pts, time_base, time_base_q);
					int64_t now_time = av_gettime() - start_time;
					if ((pts_time > now_time) && ((vid_next_pts + pts_time - now_time) < aud_next_pts))
						av_usleep(pts_time - now_time);

					ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
					av_free_packet(&enc_pkt);
				}
#endif
			}

			av_free_packet(dec_pkt);
		}
	}
}

//显示朋友的视频
int show_friend_video_thread(void *opaque)
{
	CSDLVideoDlg* pdlg = (CSDLVideoDlg*)opaque;
	AVFormatContext *pFormatCtx;
	int videoindex = 0;
	AVCodecContext  *pCodecCtx;
	AVCodec         *pCodec;

	av_register_all();
	avcodec_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	const char* input_path = "rtmp://47.52.175.85:1935/myapp/test";
	//const char* input_path = "rtmp://send1.douyu.com/live/4226085rBKFu3tXF?wsSecret=3cd5b313411b2d29a003878b52f7a635&wsTime=5a6fe284&wsSeek=off";
	AVDictionary *options = NULL;
	//av_dict_set(&options, "bufsize", "0", 0);
	if (avformat_open_input(&pFormatCtx, input_path, NULL, &options) != 0)
	{
		AfxMessageBox(L"Couldn't open input stream.");
		return -1;
	}
	pFormatCtx->probesize = 2048;
	pFormatCtx->max_analyze_duration = 1000;
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		AfxMessageBox(L"Couldn't find stream information.");
		return -1;
	}
	videoindex = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}
	}
	if (videoindex == -1)
	{
		AfxMessageBox(L"Didn't find a video stream.");
		return -1;
	}

	pCodecCtx = pFormatCtx->streams[videoindex]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		AfxMessageBox(L"Codec not found.");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		AfxMessageBox(L"Could not open codec.");
		return -1;
	}

	struct SwsContext *img_convert_ctx;
	AVFrame *pFrame, *pFrameYUV;

	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	int screen_w = pCodecCtx->width;
	int screen_h = pCodecCtx->height;

	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	sdlRenderer = SDL_CreateRenderer(pdlg->m_friend_screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);

	SDL_Rect sdlRect;
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;
	AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, screen_w, screen_h));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, screen_w, screen_h);

	img_convert_ctx = sws_getContext(screen_w, screen_h, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	while (pdlg->exit_pull_thread == 0)
	{
		if (av_read_frame(pFormatCtx, packet) < 0)
		{
			continue;
		}

		if (packet->stream_index == videoindex)
		{
			int ret, got_picture;
			ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
			if (ret < 0)
			{
				AfxMessageBox(L"Decode Error.");
				return -1;
			}

			if (got_picture)
			{
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, screen_h, pFrameYUV->data, pFrameYUV->linesize);
				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);
				//SDL_Delay(50);
			}
			av_free_packet(packet);
		}
    }

	AfxMessageBox(L"rtmp session is close.");

	return 0;
}


//CSDLVideoDlg 对话框
CSDLVideoDlg::CSDLVideoDlg(CWnd* pParent /*=NULL*/)
	: CDialog(IDD_SDLVIDEO_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	thread_exit = 0;
	exit_push_thread = 0;
	exit_pull_thread = 0;
	push_rtmp_url = (char*)malloc(128);
	strcpy(push_rtmp_url, "rtmp://47.52.175.85:1935/myapp/test");
}

void CSDLVideoDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CSDLVideoDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
END_MESSAGE_MAP()


// CSDLVideoDlg 消息处理程序

BOOL CSDLVideoDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	show_dshow_device();

	//初始化SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		MessageBox(L"Could not initialize SDL");
		return FALSE;
	}

	av_register_all();
	avdevice_register_all();
	avformat_network_init();
	m_pFormatCtx = avformat_alloc_context();

	AVDictionary *device_param = NULL;
	m_ifmt = av_find_input_format("dshow");

	av_dict_set(&device_param, "probesize", "2048", 0);
	av_dict_set(&device_param, "video_size", "800*600", 0);
	av_dict_set(&device_param, "analyzeduration", "1000", 0);

	std::vector<TDeviceName> vectorDevices;
	DS_GetAudioVideoInputDevices(vectorDevices, CLSID_VideoInputDeviceCategory);

	char video_device_name[256] = { 0 };
	for (std::vector<TDeviceName>::iterator iter = vectorDevices.begin();
		iter != vectorDevices.end();
		iter++)
	{
		sprintf(video_device_name, "video=%ws", iter->FriendlyName);
	}

	if (0 != avformat_open_input(&m_pFormatCtx, video_device_name, m_ifmt, &device_param))
	{
		MessageBox(L"Couldn't open input stream.");
		return FALSE;
	}
	//m_pFormatCtx->probesize = 2048;
	//m_pFormatCtx->max_analyze_duration = 1000;
	if (avformat_find_stream_info(m_pFormatCtx, NULL) < 0)
	{
		MessageBox(L"Couldn't find stream information.");
		return FALSE;
	}
	m_videoindex = -1;
	for (int i = 0; i < m_pFormatCtx->nb_streams; i++)
	{
	    if (m_pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
	    {
		    m_videoindex = i;
		    break;
	    }
    }
	if (m_videoindex == -1) 
	{
		MessageBox(L"Didn't find a video stream.");
		return FALSE;
	}

	m_pCodecCtx = m_pFormatCtx->streams[m_videoindex]->codec;
	m_pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
	if (m_pCodec == NULL)
	{
		MessageBox(L"Codec not found.");
		return FALSE;
	}
	if (avcodec_open2(m_pCodecCtx, m_pCodec, NULL)<0)
	{
		MessageBox(L"Could not open codec.");
		return FALSE;
	}

	CWnd* pWnd = this->GetDlgItem(IDC_VIDEO_STATIC); //IDC_VIDEO_STATIC就是图像控件的ID
	HWND hpictureWnd = pWnd->GetSafeHwnd();          //获取图像控件的句柄  
	m_screen = SDL_CreateWindowFrom(hpictureWnd);    //SDL创建窗口时，把句柄传入即可

	//decode_video_tid = SDL_CreateThread(decode_video_thread, NULL, this);

	CWnd* pfWnd = GetDlgItem(IDC_VIDEO_STATIC2); //IDC_VIDEO_STATIC就是图像控件的ID
	HWND hfpictureWnd = pfWnd->GetSafeHwnd();          //获取图像控件的句柄  
	m_friend_screen = SDL_CreateWindowFrom(hfpictureWnd);    //SDL创建窗口时，把句柄传入即可
	
	push_local_av_tid = SDL_CreateThread(push_local_av_thread, NULL, this);

	show_friend_video_tid = SDL_CreateThread(show_friend_video_thread, NULL, this);

	return TRUE;
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。
void CSDLVideoDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

void CSDLVideoDlg::show_dshow_device() 
{
	AVFormatContext *pFmtCtx = avformat_alloc_context();
	AVDictionary* options = NULL;
	av_dict_set(&options, "list_devices", "true", 0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	printf("Device Info=============\n");
	avformat_open_input(&pFmtCtx, "video=dummy", iformat, &options);
	printf("========================\n");
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CSDLVideoDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

