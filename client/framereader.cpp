/*
 * File: framereader.cpp
 * Author: Justin Williams
 * Date: 2/21/26
 * File Description: A class implementing a frame reader. The frame reader accepts
 * image data and converts it into a QImage, then a QVideoFrame. A QVideoSink accepts
 * a QVideoFrame and updates a VideoOutput QML object to display the frame in the app.
 */

#include "framereader.h"

FrameReader::FrameReader() {}
