#include <stdio.h>
#include <math.h>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>

int main() {
    CvCapture* capture = cvCaptureFromCAM(0); // '0'은 첫 번째 카메라 장치를 의미합니다.
    if (!capture) {
        fprintf(stderr, "카메라를 열 수 없습니다.\n");
        return -1;
    }

    cvSetCaptureProperty(capture, CV_CAP_PROP_FPS, 60);

    cvNamedWindow("타겟 인식", CV_WINDOW_AUTOSIZE);
    cvNamedWindow("처리된 영상", CV_WINDOW_AUTOSIZE);

    IplImage* frame;
    IplImage* gray;
    IplImage* blurred;
    IplImage* edge;
    CvMemStorage* storage = cvCreateMemStorage(0);

    // 빨간색 필터링을 위한 HSV 범위
    CvScalar lower_red = cvScalar(0, 120, 70, 0);
    CvScalar upper_red = cvScalar(10, 255, 255, 0);
    CvScalar lower_red2 = cvScalar(160, 120, 70, 0);
    CvScalar upper_red2 = cvScalar(180, 255, 255, 0);

    IplImage* hsv;
    IplImage* redMask;
    IplImage* redMask2;
    IplImage* blackMask;

    int score = 0; // 점수 저장 변수

    while (1) {
        frame = cvQueryFrame(capture);
        if (!frame) break;

        gray = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        blurred = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        edge = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        blackMask = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);

        cvCvtColor(frame, gray, CV_BGR2GRAY);
        cvSmooth(gray, blurred, CV_GAUSSIAN, 9, 9, 0, 0);
        cvCanny(blurred, edge, 5, 30, 3);

        // BGR을 HSV로 변환
        hsv = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
        cvCvtColor(frame, hsv, CV_BGR2HSV);

        // 빨간색 범위 필터링
        redMask = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        redMask2 = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        cvInRangeS(hsv, lower_red, upper_red, redMask);
        cvInRangeS(hsv, lower_red2, upper_red2, redMask2);
        cvOr(redMask, redMask2, redMask, NULL);

        // 검은색 필터링
        cvInRangeS(gray, cvScalar(0), cvScalar(50), blackMask);

        CvSeq* contours;
        cvFindContours(edge, storage, &contours, sizeof(CvContour), CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));

        // 처리된 이미지 준비
        IplImage* processed = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
        cvSet(processed, cvScalar(255, 255, 255, 0), NULL); // 하얀색으로 초기화

        while (contours) {
            double area = cvContourArea(contours, CV_WHOLE_SEQ, 0);
            if (area > 100) { // 면적이 100 이상인 원만 인식
                CvBox2D ellipse = cvFitEllipse2(contours);
                cvEllipseBox(processed, ellipse, CV_RGB(255, 0, 0), 2, 8, 0);

                // 원의 면적에 따른 점수 할당
                int redScore = (int)(10000 / area); // 예시 점수 계산 방식

                // 검은 원 안에 빨간 원이 있는지 확인
                if (cvPointPolygonTest(contours, cvPoint2D32f(ellipse.center.x, ellipse.center.y), 0) > 0) {
                    score += redScore;
                }
            }
            contours = contours->h_next;
        }

        cvShowImage("타겟 인식", frame);
        cvShowImage("처리된 영상", processed);

        char c = cvWaitKey(30);
        if (c == 'q') break;

        // 리소스 해제
        cvReleaseImage(&processed);
        cvClearMemStorage(storage);
        cvReleaseImage(&gray);
        cvReleaseImage(&blurred);
        cvReleaseImage(&edge);
        cvReleaseImage(&hsv);
        cvReleaseImage(&redMask);
        cvReleaseImage(&redMask2);
        cvReleaseImage(&blackMask);
    }

    cvReleaseCapture(&capture);
    cvDestroyWindow("타겟 인식");
    cvDestroyWindow("처리된 영상");
    cvReleaseMemStorage(&storage);

    printf("Final Score: %d\n", score);

    return 0;
}
