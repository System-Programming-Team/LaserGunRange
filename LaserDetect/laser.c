#include <stdio.h>
#include <math.h>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

bool waiting = true;
uint8_t led;

void *communicate_data(void *sock);

typedef struct{
    float index;
    float indey;
    float indeh;
}Coordinate;

int compare(const void* a, const void* b) {
    Coordinate* coordA = (Coordinate*)a;
    Coordinate* coordB = (Coordinate*)b;
    return (coordA->index - coordB->index);
}
Coordinate* averageAndGroup(Coordinate* coords, int length, int* newLength) {
    qsort(coords, length, sizeof(Coordinate), compare);

    Coordinate* newCoords = (Coordinate*)malloc(sizeof(Coordinate) * length);
    Coordinate temp;
    int j = 0;
    
    for(int i = 0; i < length-1; i++) {
        if(coords[i+1].index-coords[i].index<3){
          coords[i+1]=coords[i];
          coords[i].index=-1;
        }
    }
    
    for(int i=0; i<length; i++) {
      if(coords[i].index==-1) continue;
      newCoords[j] = coords[i];
      j+=1;
    }
    *newLength = j;
    
    return newCoords;
}

int main(int argc, char *argv[]){
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    // 파일 디스크립터, 포트, 서버 주소, send, receive 스레드 저장할 변수 선언
    int fd, port=atoi(argv[2]);
    struct sockaddr_in server_addr;
    pthread_t communicate_thread;
    // 소켓생성을 위한 argument의 서버 주소, domain, port 지정
    fd=socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(port);

        // argument에서 받아온 주소 저장
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    // 제공된 정보를 기반으로 서버에 연결요청.
    if(connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr))<0){
        fprintf(stderr, "[*] connection failed\n");
        return 1;
    }

    Coordinate coords[10];
    CvCapture* capture = cvCaptureFromCAM(0);
    if (!capture) {
        fprintf(stderr, "카메라를 열 수 없습니다.\n");
        return -1;
    }

    //cvNamedWindow("타겟 인식", CV_WINDOW_AUTOSIZE);
    //cvNamedWindow("처리된 영상", CV_WINDOW_AUTOSIZE);
    
    IplImage* frame;
    IplImage* gray;
    IplImage* blurred;
    IplImage* edge;
    IplImage* hsv;
    IplImage* redMask;
    CvMemStorage* storage = cvCreateMemStorage(0);
    
    int score = 0;
    
    while (1) {
        frame = cvQueryFrame(capture);
        if (!frame) break;
        int order=0;
        uint send, recv=3;
    
        gray = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        blurred = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        edge = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        hsv = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
        redMask = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
    
        cvCvtColor(frame, gray, CV_BGR2GRAY);
        cvSmooth(gray, blurred, CV_GAUSSIAN, 9, 9, 0, 0);
        cvCanny(blurred, edge, 5, 30, 3);
    
        cvCvtColor(frame, hsv, CV_BGR2HSV);
        cvInRangeS(hsv, cvScalar(0, 150, 150, 0), cvScalar(10, 255, 255, 0), redMask);
    
        CvSeq* contours1;
        CvSeq* contours2;
        cvFindContours(edge, storage, &contours1, sizeof(CvContour), CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));
        cvFindContours(edge, storage, &contours2, sizeof(CvContour), CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));
    
        // 처리된 이미지 준비
        IplImage* processed = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
        cvSet(processed, cvScalar(255, 255, 255, 0), NULL); // 하얀색으로 초기화

        while(contours1){
            if(contours1->total>=5){
                double area = cvContourArea(contours1, CV_WHOLE_SEQ, 0);
                double perimeter = cvArcLength(contours1, CV_WHOLE_SEQ, 1);
                double circularity = 4 * M_PI * area / (perimeter * perimeter);

                CvBox2D ellipseCircle;

                if(area>200 && circularity > 0.3){
                    ellipseCircle = cvFitEllipse2(contours1);
                    cvEllipseBox(processed, ellipseCircle, CV_RGB(0, 255, 0), 2, 8, 0);  // 초록색 원 그리기

                    coords[order].index=ellipseCircle.center.x;
                    coords[order].indey=ellipseCircle.center.y;
                    coords[order].indeh=ellipseCircle.size.height;
                    order++;
                }
            }
            contours1=contours1->h_next;
        }
        int newLength;
        Coordinate* newCoords = averageAndGroup(coords, order, &newLength);

        while (contours2) {
            if (contours2->total >= 5) {
                double area = cvContourArea(contours2, CV_WHOLE_SEQ, 0);
                double perimeter = cvArcLength(contours2, CV_WHOLE_SEQ, 1);
                double circularity = 4 * M_PI * area / (perimeter * perimeter);
                
                CvBox2D ellipseLaser;  // 레이저 포인트의 원을 나타내는 변수
                
                if (area <= 200 && circularity > 0.3) { // 레이저 포인트를 원으로 인식 (크기가 20 이하)
                    ellipseLaser = cvFitEllipse2(contours2);
                    cvEllipseBox(processed, ellipseLaser, CV_RGB(255, 0, 0), 2, 8, 0);  // 레이저 포인트를 빨간색 원으로 표시
                    for(int i=0;i<newLength;i++){
                        double dx=ellipseLaser.center.x - coords[order].index;
                        double dy=ellipseLaser.center.y - coords[order].indey;
                        double dist=sqrt(dx*dx + dy*dy);
                        printf("Laser center %d:%f, Circle center %d:%f\n",i,ellipseLaser.center.x, i, newCoords[i].index);
                        if((recv>>i)&1){
                            if(dist<newCoords[i].indeh/2 && ellipseLaser.center.x!=0 && ellipseLaser.center.y!=0){
                                led=1<<i;
                                waiting=false;
                                printf("$d번 맞음",i);
                            }
                        }
                    }
                    if(pthread_create(&communicate_thread, NULL, communicate_data, (void*)&fd)<0){
                        fprintf(stderr, "[*] communicate thread not created.\n");
                        return 1;
                    }
                    ellipseLaser.center.x=0,ellipseLaser.center.y=0;
                    usleep(500*1000);
                    break;
                }
            }
            contours2 = contours2->h_next;
        }

    
    //    cvShowImage("타겟 인식", frame);
      //  cvShowImage("처리된 영상", processed);

    
     //   char c = cvWaitKey(30);
      //  if (c == 'q') break;
    
        cvReleaseImage(&processed);
        cvClearMemStorage(storage);
        cvReleaseImage(&gray);
        cvReleaseImage(&blurred);
        cvReleaseImage(&edge);
        cvReleaseImage(&hsv);
        cvReleaseImage(&redMask);
        free(newCoords);
    }
    
    cvReleaseCapture(&capture);
   // cvDestroyWindow("타겟 인식");
   // cvDestroyWindow("처리된 영상");
    cvReleaseMemStorage(&storage);
    
      // 스레드가 종료될 때까지 대기
    pthread_join(communicate_thread, NULL);

    close(fd);
    return 0;
}

void *communicate_data(void *sock){
    int fd=*(int*)sock;

    while(1){
        while (waiting) {};
        waiting = true;
        send(fd, &led, sizeof(led), 0);
    }
}