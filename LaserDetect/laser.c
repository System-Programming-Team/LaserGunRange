#include <cv.h>
#include <highgui.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

int main(int argc, char** argv) {

    // Target in camera view
    double CenterX = 426.5;
    double CenterY = 190.5;
    double Radius = 40.0;

    // Video capture dimensions
    int width = 800;
    int height = 640;
    
    // Open the default video camera
    CvCapture* capture = cvCaptureFromCAM(0);
    cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, width);
    cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, height);
    
    // Load the target image
    IplImage* image = cvLoadImage("target.jpg", CV_LOAD_IMAGE_COLOR);
    double target_x = image->width * 0.5;
    double target_y = image->height * 0.5;
    double target_Radius = (target_x < target_y) ? target_x : target_y;
    
    // Create a window
    cvNamedWindow("Result", CV_WINDOW_AUTOSIZE);
    
    int ShotCount = 0;
    double Score = 0;
    double largestArea = 0;

    while (1) {
        // Capture frame-by-frame
        IplImage* frame = cvQueryFrame(capture);
        if (!frame) break;

        // Convert to grayscale
        IplImage* grey_image = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 1);
        cvCvtColor(frame, grey_image, CV_BGR2GRAY);

        // Thresholding the image
        cvThreshold(grey_image, grey_image, 245, 255, CV_THRESH_BINARY);
        
        // Find contours
        CvSeq* contours;
        CvMemStorage* storage = cvCreateMemStorage(0);
        cvFindContours(grey_image, storage, &contours, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, cvPoint(0, 0));

        CvSeq* cnt = contours;
        largestArea = 0; // Reset the largestArea for each frame

        for (; cnt != NULL; cnt = cnt->h_next) {
            double area = fabs(cvContourArea(cnt, CV_WHOLE_SEQ, 0));
            if (area > largestArea) {
                largestArea = area;
                CvPoint2D32f center;
                float radius;
                cvMinEnclosingCircle(cnt, &center, &radius);

                // Assuming the ball is the largest contour
                double shot_x = (center.x - CenterX) / Radius;
                double shot_y = (center.y - CenterY) / Radius;
                double dist = sqrt(shot_x * shot_x + shot_y * shot_y);
                
                // Calculate score based on distance
                if (dist < 1.0) {
                    Score += (1.0 - dist);
                }
                
                // Draw on the target image
                CvPoint shotPoint = cvPoint(cvRound(center.x), cvRound(center.y));
                cvCircle(image, shotPoint, 5, CV_RGB(60,60,255), 10, 8, 0);
                cvCircle(image, shotPoint, 10, CV_RGB(120,120,120), 1, 8, 0);
                
                // Increment the shot count and check if the round is over
                ShotCount++;
                if (ShotCount > 6) {
                    Score /= 7.0;
                    Score *= 100.0;
                    printf("Your Score: %lf\n", Score);
                    // Reset for next round
                    Score = 0;
                    ShotCount = 0;
                    cvCopy(image, frame, NULL); // This should copy the original frame to start over
                }
            }
        }

        // Display the resulting frame
        cvShowImage("Result", frame);

        // Check for exit key
        if (cvWaitKey(1) >= 0) break;

        // Release memory
        cvReleaseImage(&grey_image);
        cvReleaseMemStorage(&storage);
    }

    // When everything done, release the capture and destroy the windows
    cvReleaseCapture(&capture);
    cvDestroyAllWindows();
    return 0;
}
