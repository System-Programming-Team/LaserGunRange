#include <stdio.h>
#include <math.h>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>

int main() {
    CvCapture* capture = cvCaptureFromCAM(0);
    if (!capture) {
        fprintf(stderr, "카메라를 열 수 없습니다.\n");
        return -1;
    }

    cvNamedWindow("타겟 인식", CV_WINDOW_AUTOSIZE);
    cvNamedWindow("처리된 영상", CV_WINDOW_AUTOSIZE);

    IplImage* frame;
    IplImage* gray;
    IplImage* blurred;
    IplImage* edge;
    CvMemStorage* storage = cvCreateMemStorage(0);

    while (1) {
        frame = cvQueryFrame(capture);
        if (!frame) break;

        gray = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        blurred = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        edge = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);

        cvCvtColor(frame, gray, CV_BGR2GRAY);
        cvSmooth(gray, blurred, CV_GAUSSIAN, 9, 9, 0, 0);
        cvCanny(blurred, edge, 5, 30, 3);

        CvSeq* contours;
        cvFindContours(edge, storage, &contours, sizeof(CvContour), CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));

        // 처리된 이미지 준비
        IplImage* processed = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
        cvSet(processed, cvScalar(255, 255, 255, 0), NULL); // 하얀색으로 초기화

        while (contours) {
            if (contours->total >= 5) {
                double area = cvContourArea(contours, CV_WHOLE_SEQ, 0);
                if (area > 6) {
                    double perimeter = cvArcLength(contours, CV_WHOLE_SEQ, 1);
                    double circularity = 4 * M_PI * area / (perimeter * perimeter);

                    if (circularity > 0.75) {
                        CvBox2D ellipse = cvFitEllipse2(contours);
                        cvEllipseBox(processed, ellipse, CV_RGB(255, 0, 0), 2, 8, 0);
                    }
                }
            }
            contours = contours->h_next;
        }

        cvShowImage("타겟 인식", frame);
        cvShowImage("처리된 영상", processed);

        char c = cvWaitKey(30);
        if (c == 'q') break;

        cvReleaseImage(&processed);
        cvClearMemStorage(storage);
        cvReleaseImage(&gray);
        cvReleaseImage(&blurred);
        cvReleaseImage(&edge);
    }

    cvReleaseCapture(&capture);
    cvDestroyWindow("타겟 인식");
    cvDestroyWindow("처리된 영상");
    cvReleaseMemStorage(&storage);

    return 0;
}
