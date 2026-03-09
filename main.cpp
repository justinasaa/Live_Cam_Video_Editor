#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <unistd.h>
#include <chrono>//sleep
using namespace std;
int imageWidth = 1280, imageHeight= 720;//1280x720 rezoliucija

std::vector<uchar> latestJpeg;
std::mutex mtx;
bool running = true;

struct Score {//JOkUBUI
    int num = 0; /// test
    int reb = 0;
    int ast = 0; 
    int stl = 0;

    int currentMode = 0; //1-7 mode
};
//##############################################################################################################
//                                                  TRANSLIAVIMAS
//##############################################################################################################
void mjpeg_server(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    std::cout << "MJPEG: http://localhost:" << port << std::endl;

    int client = accept(server_fd, nullptr, nullptr);

    std::string header =
        "HTTP/1.0 200 OK\r\n"
        "Cache-Control: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";

    send(client, header.c_str(), header.size(), 0);

    while (running)
    {
        std::vector<uchar> jpg;
        {
            std::lock_guard<std::mutex> lock(mtx);
            jpg = latestJpeg;
        }

        if (!jpg.empty())
        {
            std::string frameHeader =
                "--frame\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: " + std::to_string(jpg.size()) + "\r\n\r\n";

            send(client, frameHeader.c_str(), frameHeader.size(), 0);
            send(client, jpg.data(), jpg.size(), 0);
            send(client, "\r\n", 2, 0);
        }

        usleep(33000); // ~30 FPS
    }

    close(client);
    close(server_fd);
}
//##############################################################################################################
//                                                 SQUARE
//##############################################################################################################
void Put_Square(cv::Mat frame, int px, int py, int sizeX=10, int sizeY=10, int filled=-1, int red=100, int green=0, int blue=0, double alpha=1)
{

    cv::Mat overlay = frame.clone();

    cv::rectangle(
        overlay,
        cv::Rect(px, py, sizeX, sizeY),
        cv::Scalar(blue, green, red),
        filled==-1 ? cv::FILLED : filled 
    );

    cv::addWeighted(
        overlay, alpha,
        frame, 1.0 - alpha,
        0,
        frame
    );
}

//##############################################################################################################
//                                               PNG WITH ALPHA BLENDING
//##############################################################################################################
void Put_Png(cv::Mat frame, cv::Mat png, int px, int py)
{

    for (int y = 0; y < png.rows; y++)
    {
        for (int x = 0; x < png.cols; x++)
        {
            if(px<0 || py<0) continue;//If png is partially out of frame, skip negative columns)
            cv::Vec4b pixel = png.at<cv::Vec4b>(y, x);
            int alpha = pixel[3];
            if (alpha > 0)
            {
                int frameX = px + x;
                int frameY = py + y;
                if (frameX < frame.cols && frameY < frame.rows)
                {
                    cv::Vec3b& framePixel = frame.at<cv::Vec3b>(frameY, frameX);
                    for (int c = 0; c < 3; c++)
                    {
                        framePixel[c] = (pixel[c] * alpha + framePixel[c] * (255 - alpha)) / 255;
                    }
                }
            }
        }
    }
}
//##############################################################################################################
//                                               FIND DOT CENTER
//##############################################################################################################
void findRedDotCenter(const cv::Mat& frame, std::vector<int>& pos)
{
    pos = {-1, -1}; // Default: nerasta
    if (frame.empty())
        return;
    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

    // Raudona spalva HSV turi 2 intervalus
    cv::Mat mask1, mask2, mask;

    cv::inRange(
        hsv,
        cv::Scalar(0, 120, 70),
        cv::Scalar(10, 255, 255),
        mask1
    );

    cv::inRange(
        hsv,
        cv::Scalar(170, 120, 70),
        cv::Scalar(180, 255, 255),
        mask2
    );

    mask = mask1 | mask2;

    // Truputį išvalom triukšmą
    cv::erode(mask, mask, cv::Mat(), cv::Point(-1, -1), 2);
    cv::dilate(mask, mask, cv::Mat(), cv::Point(-1, -1), 2);

    // Randam kontūrus
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(
        mask,
        contours,
        cv::RETR_EXTERNAL,
        cv::CHAIN_APPROX_SIMPLE
    );

    if (contours.empty())
        return;

    // Pasirenkam didžiausią raudoną objektą
    double maxArea = 0.0;
    int maxIdx = -1;

    for (int i = 0; i < contours.size(); i++)
    {
        double area = cv::contourArea(contours[i]);
        if (area > maxArea)
        {
            maxArea = area;
            maxIdx = i;
        }
    }

    // Per mažas – ignoruojam
    if (maxIdx < 0 || maxArea < 20.0)//50
        return;

    // Skaičiuojam masės centrą
    cv::Moments m = cv::moments(contours[maxIdx]);
    if (m.m00 == 0)
        return;

    int centerx = static_cast<int>(m.m10 / m.m00);
    int centery = static_cast<int>(m.m01 / m.m00);
    pos[0] = centerx;
    pos[1] = centery;
    //Put_Square(frame, centerx-10, centery-10, 20);
    return;
}
//##############################################################################################################
//                                                    TEXT
//##############################################################################################################
void Put_Text(cv::Mat& frame,
              const std::string& text,
              int x, int y,
              double scale = 10.0,
              int thickness = 2,
              int r = 255, int g = 255, int b = 255,
              bool outline = true)
{
    if (frame.empty()) return;

    int fontFace = cv::FONT_HERSHEY_SIMPLEX;

    // Kad tekstas neišeitų už ribų (minimaliai apsaugom)
    int baseline = 0;
    cv::Size textSize = cv::getTextSize(text, fontFace, scale, thickness, &baseline);

    // Jei x/y per maži, pakeliam į matomą vietą
    x = std::max(0, std::min(x, frame.cols - textSize.width));
    y = std::max(textSize.height, std::min(y, frame.rows - baseline));

    // Outline (juodas kontūras) – geras matomumas
    if (outline)
    {
        cv::putText(frame, text, {x, y}, fontFace, scale,
                    cv::Scalar(0, 0, 0), thickness + 2, cv::LINE_AA);
    }

    // Pagrindinis tekstas
    cv::putText(frame, text, {x, y}, fontFace, scale,
                cv::Scalar(b, g, r), thickness, cv::LINE_AA);
}
//##############################################################################################################
//                                              ALPHA BLEND
//##############################################################################################################
static void AlphaBlendBGRA(cv::Mat& frameBGR, const cv::Mat& overlayBGRA, int x0, int y0)
{
    CV_Assert(frameBGR.type() == CV_8UC3);
    CV_Assert(overlayBGRA.type() == CV_8UC4);

    for (int y = 0; y < overlayBGRA.rows; ++y)
    {
        int fy = y0 + y;
        if (fy < 0 || fy >= frameBGR.rows) continue;

        const cv::Vec4b* srcRow = overlayBGRA.ptr<cv::Vec4b>(y);
        cv::Vec3b* dstRow = frameBGR.ptr<cv::Vec3b>(fy);

        for (int x = 0; x < overlayBGRA.cols; ++x)
        {
            int fx = x0 + x;
            if (fx < 0 || fx >= frameBGR.cols) continue;

            cv::Vec4b s = srcRow[x];
            int a = s[3];
            if (a == 0) continue;

            cv::Vec3b& d = dstRow[fx];
            // integer alpha blend
            d[0] = (s[0] * a + d[0] * (255 - a)) / 255;
            d[1] = (s[1] * a + d[1] * (255 - a)) / 255;
            d[2] = (s[2] * a + d[2] * (255 - a)) / 255;
        }
    }
}

//##############################################################################################################
//                                              ROTATED TEXT
//##############################################################################################################
void PutTextRotated(cv::Mat& frame,
                    const std::string& text,
                    int cx, int cy,
                    double angleDeg,
                    double scale = 1.0,
                    int thickness = 2,
                    cv::Scalar bgr = cv::Scalar(255,255,255),
                    bool outline = true)
{
    if (frame.empty()) return;

    int font = cv::FONT_HERSHEY_SIMPLEX;
    int baseline = 0;
    cv::Size ts = cv::getTextSize(text, font, scale, thickness, &baseline);

    // drobė tekstui (BGRA) su paraštėm, kad tilptų pasukus
    int pad = 20;
    int w = ts.width + pad * 2;
    int h = ts.height + baseline + pad * 2;

    cv::Mat canvas(h, w, CV_8UC4, cv::Scalar(0,0,0,0));

    cv::Point org(pad, pad + ts.height); // putText bazė

    if (outline)
        cv::putText(canvas, text, org, font, scale, cv::Scalar(0,0,0,255), thickness + 2, cv::LINE_AA);

    cv::putText(canvas, text, org, font, scale, cv::Scalar(bgr[0], bgr[1], bgr[2], 255), thickness, cv::LINE_AA);

    // pasukam
    cv::Point2f center(canvas.cols/2.0f, canvas.rows/2.0f);
    cv::Mat M = cv::getRotationMatrix2D(center, angleDeg, 1.0);

    cv::Mat rotated;
    cv::warpAffine(canvas, rotated, M, canvas.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0,0,0,0));

    // uždedam į frame
    int x0 = cx - rotated.cols/2;
    int y0 = cy - rotated.rows/2;
    AlphaBlendBGRA(frame, rotated, x0, y0);
}
//##############################################################################################################
//                                            PERSPECTIVE TEXT
//##############################################################################################################
void PutTextPerspective(cv::Mat& frame,
                        const std::string& text,
                        const std::array<cv::Point2f,4>& dstQuad,
                        double scale = 1.0,
                        int thickness = 2,
                        cv::Scalar bgr = cv::Scalar(255,255,255),
                        bool outline = true)
{
    if (frame.empty()) return;

    int font = cv::FONT_HERSHEY_SIMPLEX;
    int baseline = 0;
    cv::Size ts = cv::getTextSize(text, font, scale, thickness, &baseline);

    int pad = 20;
    int w = ts.width + pad*2;
    int h = ts.height + baseline + pad*2;

    // Šaltinis: tekstas ant BGRA drobės
    cv::Mat src(h, w, CV_8UC4, cv::Scalar(0,0,0,0));
    cv::Point org(pad, pad + ts.height);

    if (outline)
        cv::putText(src, text, org, font, scale, cv::Scalar(0,0,0,255), thickness + 2, cv::LINE_AA);
    cv::putText(src, text, org, font, scale, cv::Scalar(bgr[0], bgr[1], bgr[2], 255), thickness, cv::LINE_AA);

    // Šaltinio keturkampis (visa drobė)
    std::array<cv::Point2f,4> srcQuad = {
        cv::Point2f(0,0),
        cv::Point2f((float)w-1, 0),
        cv::Point2f((float)w-1, (float)h-1),
        cv::Point2f(0, (float)h-1)
    };

    cv::Mat H = cv::getPerspectiveTransform(srcQuad.data(), dstQuad.data());

    // Warpinam į full-frame dydį (kad galėtume alpha-blendinti tiesiogiai)
    cv::Mat warped(frame.rows, frame.cols, CV_8UC4, cv::Scalar(0,0,0,0));
    cv::warpPerspective(src, warped, H, warped.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT,                    cv::Scalar(0,0,0,0));

    AlphaBlendBGRA(frame, warped, 0, 0);
}
cv::Mat OpenImage(std::string path)
{
        cv::Mat png = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (png.empty() || png.channels() != 4)
    {
        std::cerr << "png nerastas arba be alpha\n";
        return cv::Mat();
    }

    return png;
}
//##############################################################################################################
//                                                  INFO TABLE
//##############################################################################################################
void infoTable(cv::Mat& frame, std::string imagePathA, std::string imagePathB, int resultA, int resultB)
{
    int pinkR = 255, pinkG= 192, pinkB = 203;
    int orangeR = 251, orangeG = 206, orangeB = 177;
    //frame, x, y, sizeX, sizeY, r, g, b
    //Black square
    Put_Square(frame, 100, 100, imageWidth-200, imageHeight-200, -1, 0, 0, 0, 0.5);
    //Vertical pink square
    Put_Square(frame, imageWidth/2-10, 150, 20, imageHeight-250, -1 ,orangeR, orangeG, orangeB);
    //Horizontal pink square
    Put_Square(frame, 100, 150, imageWidth-200, 20, -1 ,orangeR, orangeG, orangeB);
    //Aplink ratu pink
    Put_Square(frame, 100, 100, imageWidth-200, imageHeight-200, 10,orangeR, orangeG, orangeB);
    //Rezultatas
    Put_Text(frame, "Rezultatas", imageWidth/2-100, 140, 1, 2, 255, 255, 255);
    cv::Mat pngA = OpenImage(imagePathA);
    std::cout<<pngA.cols<<" "<<pngA.rows<<std::endl;
    cv::resize(pngA, pngA, cv::Size(), 100.0/(double)pngA.cols, 100.0/(double)pngA.rows);
    Put_Png(frame, pngA, imageWidth/2-100-10, 175);
    cv::Mat pngB = OpenImage(imagePathB);
    std::cout<<pngB.cols<<" "<<pngB.rows<<std::endl;
    cv::resize(pngB, pngB, cv::Size(), 100.0/(double)pngB.cols, 100.0/(double)pngB.rows);
    Put_Png(frame, pngB, imageWidth/2+15, 175);
    Put_Text(frame, std::to_string(resultA), 105, 250, 3, 4, 255, 255, 255);
    Put_Text(frame, std::to_string(resultB), imageWidth/2-100+445, 250, 3, 4, 255, 255, 255);


}
//##############################################################################################################
//                                                     USER INTERFACE
//##############################################################################################################

void UserInterface( Score* score1, mutex* score1m)
{
    (*score1m).lock();
    cout<<"USER INTERFEACE num="<<(*score1).num<<endl;
    (*score1).num=69;
    (*score1m).unlock();

    
}
//##############################################################################################################
//                                                     MAIN
//##############################################################################################################
int main()
{
    //OPTIONS
    cout<<"Rezimo pasirinkimas\n";
    cout<<"1 karuna + text\n 2 lentelė\n";
    int mode;
    cin>>mode;

    //JOKUBO NUO
    mutex score1m;//Uzraktui
    Score score1;//struktura su rezultatu
    cout<<"Main num="<<score1.num<<endl;
    score1.num=0;
    cout<<"Main num="<<score1.num<<endl;
    thread UI(UserInterface, &score1, &score1m);//Kažkaip užbaigt UI thread
    //JOKUBO IKI
    

//XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

    // Open cam
    cv::VideoCapture cap(0);
    if (!cap.isOpened())
    {
        std::cerr << "Nepavyko atidaryti kameros\n";
        return 1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, imageWidth);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, imageHeight);

    
    // Open server port 8080
    std::thread server(mjpeg_server, 8080);

    cv::Mat frame;

    while (true)//INFINITE LOOP
    {
        cap >> frame;
        if (frame.empty()) continue;

        cout<<"Main num="<<score1.num<<endl;//JOKUBUI
        

        // JPEG encode
        std::vector<uchar> buf;
        cv::imencode(".jpg", frame, buf, {cv::IMWRITE_JPEG_QUALITY, 80});

        {
            std::lock_guard<std::mutex> lock(mtx);
            latestJpeg = buf;
        }
    }

    running = false;
    server.join();
    //UI.join();
    return 0;
}
















