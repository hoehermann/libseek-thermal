// Seek Thermal Viewer/Streamer
// http://github.com/fnoop/maverick

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "seek.h"
#include "SeekCam.h"
#include <iostream>
#include <string>
#include <signal.h>
#include <memory>
#include "args.h"

using namespace cv;
using namespace LibSeek;

// Setup sig handling
static volatile sig_atomic_t sigflag = 0;
void handle_sig(int sig) {
    (void)sig;
    sigflag = 1;
}

double temp_from_raw(int x) {
    // Known measurements (SeekPro):
    // 0C => 273K => 13500 raw (ice)
    // 19C => 292K => 14396 raw (my room temperature)
    // 36C => 309K => 16136 raw (my body temp, more or less)
    // 100C => 373K => 20300 raw (freshely boiled water)
    // 330C => 603K => 32768 raw (known upper limit, full 15 bits - 2^15)
    //
    // All values above perfectly demonstrate linear tendency in Excel.
    // Constants below are taken from linear trend line in Excel.
    // -273 is translation of Kelvin to Celsius
    return (double) (0.0171156038 * x + 37) - 273;
}

void overlay_values(Mat &outframe, double temp, Point &coord, Scalar color) {
    int gap=2;
    int arrLen=7;
    arrowedLine(outframe, coord-Point(-arrLen, -arrLen), coord-Point(-gap, -gap), color, 1.5, LINE_AA, 0, 0.2);
    arrowedLine(outframe, coord-Point(arrLen, arrLen), coord-Point(gap, gap), color, 1.5, LINE_AA, 0, 0.2);
    arrowedLine(outframe, coord-Point(-arrLen, arrLen), coord-Point(-gap, gap), color, 1.5, LINE_AA, 0, 0.2);
    arrowedLine(outframe, coord-Point(arrLen, -arrLen), coord-Point(gap, -gap), color, 1.5, LINE_AA, 0, 0.2);

    char txt [6];
    sprintf(txt, "%5.1f", temp);
    putText(outframe, txt, coord-Point(21, -21), FONT_HERSHEY_COMPLEX_SMALL, 0.75, Scalar(255, 255, 255), 1, CV_AA);
    putText(outframe, txt, coord-Point(19, -19), FONT_HERSHEY_COMPLEX_SMALL, 0.75, Scalar(0, 0, 0), 1, CV_AA);
    putText(outframe, txt, coord-Point(20, -20), FONT_HERSHEY_COMPLEX_SMALL, 0.75, color, 1, CV_AA);
}

// Function to process a raw (corrected) seek frame
void process_frame(Mat &inframe, Mat &outframe, float scale, int colormap, int rotate, int device_temp) {
    Mat frame_g8, frame_g16; // Transient Mat containers for processing

    // from https://stackoverflow.com/questions/12521874/how-to-compute-maximum-pixel-value-of-mat-in-opencv
    // values before normalize is Kelvins, represented somehow
    double min, max;
    minMaxIdx(inframe, &min, &max);

    double mintemp=temp_from_raw(min);
    double maxtemp=temp_from_raw(max);
    printf("rmin,rmax,devtemp: %d %d %d / min-max: %.1f %.1f\n", (int)min, (int)max, (int)device_temp, mintemp, maxtemp);

    normalize(inframe, frame_g16, 0, 65535, NORM_MINMAX);

    // Convert seek CV_16UC1 to CV_8UC1
    frame_g16.convertTo(frame_g8, CV_8UC1, 1.0/256.0 );

    // Rotate image
    if (rotate == 90) {
        transpose(frame_g8, frame_g8);
        flip(frame_g8, frame_g8, 1);
    } else if (rotate == 180) {
        flip(frame_g8, frame_g8, -1);
    } else if (rotate == 270) {
        transpose(frame_g8, frame_g8);
        flip(frame_g8, frame_g8, 0);
    }

    Point minp, maxp;
    minMaxLoc(frame_g8, NULL, NULL, &minp, &maxp); // doing it here, so we take rotation into account
    minp*=scale;
    maxp*=scale;

    // Resize image: http://docs.opencv.org/3.2.0/da/d54/group__imgproc__transform.html#ga5bb5a1fea74ea38e1a5445ca803ff121
    // Note this is expensive computationally, only do if option set != 1
    if (scale != 1.0)
        resize(frame_g8, frame_g8, Size(), scale, scale, INTER_LINEAR);

    // Apply colormap: http://docs.opencv.org/3.2.0/d3/d50/group__imgproc__colormap.html#ga9a805d8262bcbe273f16be9ea2055a65
    if (colormap != -1) {
        applyColorMap(frame_g8, outframe, colormap);
    } else {
        cv::cvtColor(frame_g8, outframe, cv::COLOR_GRAY2BGR);
    }

    overlay_values(outframe, mintemp, minp, Scalar(255,0,0));
    overlay_values(outframe, maxtemp, maxp, Scalar(0,0,255));

    // TODO: extend image with gradient and put numbers onto this gradient
    // TODO: have option to display max/min at their corresponding image coordinates
    // sorry, I'm not C++ developer
}

int main(int argc, char** argv)
{
    // Setup arguments for parser
    args::ArgumentParser parser("Seek Thermal Viewer");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> _output(parser, "output", "Output Stream - name of the video file to write", {'o', "output"});
    args::ValueFlag<std::string> _ffc(parser, "FFC", "Additional Flat Field calibration - provide ffc file", {'F', "FFC"});
    args::ValueFlag<int> _fps(parser, "fps", "Video Output FPS - Kludge factor", {'f', "fps"});
    args::ValueFlag<float> _scale(parser, "scaling", "Output Scaling - multiple of original image", {'s', "scale"});
    args::ValueFlag<int> _colormap(parser, "colormap", "Color Map - number between 0 and 12", {'c', "colormap"});
    args::ValueFlag<int> _rotate(parser, "rotate", "Rotation - 0, 90, 180 or 270 (default) degrees", {'r', "rotate"});
    args::ValueFlag<std::string> _camtype(parser, "camtype", "Seek Thermal Camera Model - seek or seekpro", {'t', "camtype"});

    // Parse arguments
    try
    {
        parser.ParseCLI(argc, argv);
    }
    catch (args::Help)
    {
        std::cout << parser;
        return 0;
    }
    catch (args::ParseError e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    catch (args::ValidationError e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    float scale = 1.0;
    if (_scale)
        scale = args::get(_scale);
    std::string output = "window";
    if (_output)
        output = args::get(_output);
    std::string camtype = "seek";
    if (_camtype)
        camtype = args::get(_camtype);
    // 7fps seems to be about what you get from a seek thermal compact
    // Note: fps doesn't influence how often frames are processed, just the VideoWriter interpolation
    int fps = 9; // from APK of Seek app I saw that they support 9hz
    if (camtype == "seekpro") {
        fps = 15; //  works fine for my SeekPro
    }
    if (_fps)
        fps = args::get(_fps);
    // Colormap int corresponding to enum: http://docs.opencv.org/3.2.0/d3/d50/group__imgproc__colormap.html
    int colormap = -1;
    if (_colormap)
        colormap = args::get(_colormap);
    // Rotate default is landscape view to match camera logo/markings
    int rotate = 270;
    if (_rotate)
        rotate = args::get(_rotate);

    // Register signals
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    // Setup seek camera
    LibSeek::SeekCam* seek;
    LibSeek::SeekThermalPro seekpro(args::get(_ffc));
    LibSeek::SeekThermal seekclassic(args::get(_ffc));
    if (camtype == "seekpro")
        seek = &seekpro;
    else
        seek = &seekclassic;

    if (!seek->open()) {
        std::cout << "Error accessing camera" << std::endl;
        return 1;
    }

    // Mat containers for seek frames
    Mat seekframe, outframe;

    // Retrieve a single frame, resize to requested scaling value and then determine size of matrix
    //  so we can size the VideoWriter stream correctly
    if (!seek->read(seekframe)) {
        std::cout << "Failed to read initial frame from camera, exiting" << std::endl;
        return -1;
    }

    printf("WxH: %d %d\n", seekframe.cols, seekframe.rows);

    process_frame(seekframe, outframe, scale, colormap, rotate, seek->device_temp());

    // Create an output object, if output specified then setup the pipeline unless output is set to 'window'
    VideoWriter writer;
    if (output != "window") {
        writer.open(output, CV_FOURCC('F', 'M', 'P', '4'), fps, Size(outframe.cols, outframe.rows));
        if (!writer.isOpened()) {
            std::cerr << "Error can't create video writer" << std::endl;
            return 1;
        }

        std::cout << "Video stream created, dimension: " << outframe.cols << "x" << outframe.rows << ", fps:" << fps << std::endl;
    }

    // Main loop to retrieve frames from camera and output
    while (!sigflag) {

        // If signal for interrupt/termination was received, break out of main loop and exit
        if (!seek->read(seekframe)) {
            std::cout << "Failed to read frame from camera, exiting" << std::endl;
            return -1;
        }

        // Retrieve frame from seek and process
        process_frame(seekframe, outframe, scale, colormap, rotate, seek->device_temp());

        if (output == "window") {
            imshow("SeekThermal", outframe);
            char c = waitKey(10);
            if (c == 's') {
                waitKey(0);
            }
        } else {
            writer << outframe;
        }
    }

    std::cout << "Break signal detected, exiting" << std::endl;
    return 0;
}
