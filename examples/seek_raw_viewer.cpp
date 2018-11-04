// Seek Thermal Viewer/Streamer
// http://github.com/fnoop/maverick

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "seek.h"
#include "SeekCam.h"
#include <iostream>
#include <string>
#include <signal.h>
#include <math.h>
#include <memory>
#include "args.h"

using namespace LibSeek;

// Setup sig handling
static volatile sig_atomic_t sigflag = 0;
void handle_sig(int sig) {
    (void)sig;
    sigflag = 1;
}

int main(int argc, char** argv)
{
    // Setup arguments for parser
    args::ArgumentParser parser("Seek Thermal Viewer");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> _ffc(parser, "FFC", "Additional Flat Field calibration - provide ffc file", {'F', "FFC"});
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
    std::string camtype = "seek";
    if (_camtype)
        camtype = args::get(_camtype);

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

    printf("#rmin rmax central devtempsns\n");

    // Mat containers for seek frames
    cv::Mat seekframe, outframe;

    // Main loop to retrieve frames from camera and output
    while (!sigflag) {

        // If signal for interrupt/termination was received, break out of main loop and exit
        if (!seek->read(seekframe)) {
            std::cout << "Failed to read frame from camera, exiting" << std::endl;
            return -1;
        }

        // Retrieve frame from seek and process

        // get raw max/min/central values
        double min, max, central;
        cv::minMaxIdx(seekframe, &min, &max);
        cv::Point center(seekframe.cols/2.0, seekframe.rows/2.0);
        cv::Rect middle(center-cv::Point(1,1), cv::Size(3,3));
        cv::Scalar mean = cv::mean(seekframe(middle));
        central = mean(0);

        printf("%d %d %d %d\n", 
        int(min), int(max), int(central), int(seek->device_temp_sensor()));

        seekframe.convertTo(outframe, CV_8U, 1, 128-central);

        if (!outframe.empty()) {
            cv::imshow("SeekThermal", outframe);
        }
        char c = cv::waitKey(10);
        if (c == 'q') {
            break;
        }
    }

    if (sigflag) {
        std::cout << "Break signal detected, exiting" << std::endl;
    }
    
    return 0;
}
