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
#include <time.h>

bool waiting = true;
uint8_t led;

#define CLIENT 0
#define OPEN 1

void *communicate_data(void *sock);

typedef struct{
    float index;
    float indey;
    float indeh;
}Coordinate;

void check_and_add(Coordinate coords[], int *size, Coordinate new_coords){
    for(int i = 0; i < *size; i++) {
        if(abs(coords[i].index - new_coords.index) <= 3) {
            return; // 새로운 값과 기존 값의 차이가 3 이하라면 함수 종료
        }
    }

    // 새로운 값과 기존 값의 차이가 모두 3 초과라면 배열에 추가
    coords[*size] = new_coords;
    (*size)++;
}
// 사용자 정의 비교 함수
int compare(const void *a, const void *b) {
    Coordinate *coordA = (Coordinate *)a;
    Coordinate *coordB = (Coordinate *)b;

    if (coordA->indeh < coordB->indeh) return 1;
    else if (coordA->indeh > coordB->indeh) return -1;
    else return 0;
}

int main(int argc, char *argv[]){
#if CLIENT
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
#endif
    time_t start,end;
    Coordinate coords[10];
    CvCapture* capture = cvCaptureFromCAM(0);
    if (!capture) {
        fprintf(stderr, "카메라를 열 수 없습니다.\n");
        return -1;
    }
#if OPEN
    cvNamedWindow("타겟 인식", CV_WINDOW_AUTOSIZE);
    cvNamedWindow("처리된 영상", CV_WINDOW_AUTOSIZE);
#endif
    IplImage* frame;
    IplImage* gray;
    IplImage* blurred;
    IplImage* edge;

    CvMemStorage* storage = cvCreateMemStorage(0);
    
    int score = 0;
    
    while (1) {
        frame = cvQueryFrame(capture);
        if (!frame) break;
        int point=0;
        uint send, recv=3;
    
        gray = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        blurred = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        edge = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
    
        cvCvtColor(frame, gray, CV_BGR2GRAY);
        cvSmooth(gray, blurred, CV_GAUSSIAN, 9, 9, 0, 0);
        cvCanny(blurred, edge, 5, 30, 3);
    
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
                    Coordinate new_coords;
                    new_coords.index=ellipseCircle.center.x;
                    new_coords.indey=ellipseCircle.center.y;
                    new_coords.indeh=ellipseCircle.size.height;
                    check_and_add(coords, &point, new_coords);
                }
            }
            contours1=contours1->h_next;
        }
        qsort(coords, point, sizeof(Coordinate), compare); // qsort 함수로 정렬

        while (contours2) {
            if (contours2->total >= 5) {
                double area = cvContourArea(contours2, CV_WHOLE_SEQ, 0);
                double perimeter = cvArcLength(contours2, CV_WHOLE_SEQ, 1);
                double circularity = 4 * M_PI * area / (perimeter * perimeter);
                
                CvBox2D ellipseLaser;  // 레이저 포인트의 원을 나타내는 변수
                
                if (area <= 200 && circularity > 0.3) { // 레이저 포인트를 원으로 인식 (크기가 20 이하)
                    ellipseLaser = cvFitEllipse2(contours2);
                    cvEllipseBox(processed, ellipseLaser, CV_RGB(255, 0, 0), 2, 8, 0);  // 레이저 포인트를 빨간색 원으로 표시
                    for(int i=0;i<point;i++){
                        double dx=ellipseLaser.center.x - coords[i].index;
                        double dy=ellipseLaser.center.y - coords[i].indey;
                        double dist=sqrt(dx*dx + dy*dy);
                        end=time(NULL);
                        if(dist<coords[i].indeh/2 && ellipseLaser.center.x!=0 && ellipseLaser.center.y!=0 && difftime(end,start)>0.5){
                            printf("%d번 맞음\n",i);
                            #if CLIENT
                            if((recv>>i)&1){
                                led=1<<i;
                                waiting=false;
                            }
                            #endif
                            ellipseLaser.center.x=0,ellipseLaser.center.y=0;
                            start=time(NULL);
                            break;
                        }
                    }
                    #if CLIENT
                    if(pthread_create(&communicate_thread, NULL, communicate_data, (void*)&fd)<0){
                        fprintf(stderr, "[*] communicate thread not created.\n");
                        return 1;
                    }
                    #endif
                }
            }
            contours2 = contours2->h_next;
        }

#if OPEN    
        cvShowImage("타겟 인식", frame);
        cvShowImage("처리된 영상", processed);

        char c = cvWaitKey(30);
        if (c == 'q') break;
#endif    
        cvReleaseImage(&processed);
        cvClearMemStorage(storage);
        cvReleaseImage(&gray);
        cvReleaseImage(&blurred);
        cvReleaseImage(&edge);
    }
    
    cvReleaseCapture(&capture);
#if OPEN
    cvDestroyWindow("타겟 인식");
    cvDestroyWindow("처리된 영상");
#endif
    cvReleaseMemStorage(&storage);
#if CLIENT
      // 스레드가 종료될 때까지 대기
    pthread_join(communicate_thread, NULL);
    close(fd);
#endif
    return 0;
}
#if CLIENT
void *communicate_data(void *sock){
    int fd=*(int*)sock;

    while(1){
        while (waiting) {};
        waiting = true;
        send(fd, &led, sizeof(led), 0);
    }
}
#endif