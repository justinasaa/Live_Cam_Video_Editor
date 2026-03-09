#test
#include <opencv2/opencv.hpp>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

int main() {
    cv::VideoCapture cap("videos/translate.mp4");
    if (!cap.isOpened()) {
        std::cerr << "Nepavyko atidaryti video\n";
        return -1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8090); // NE 8080
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    std::cout << "Stream: http://localhost:8090\n";

    int client = accept(server_fd, NULL, NULL);

    std::string header =
        "HTTP/1.0 200 OK\r\n"
        "Server: mjpeg-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Cache-Control: private\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";

    send(client, header.c_str(), header.size(), 0);

    cv::Mat frame;
    std::vector<uchar> buffer;

    while (true) {

        if (!cap.read(frame)) {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0); // loop
            continue;
        }

        cv::imencode(".jpg", frame, buffer);

        std::string part =
            "--frame\r\n"
            "Content-Type: image/jpeg\r\n\r\n";

        send(client, part.c_str(), part.size(), 0);
        send(client, (char*)buffer.data(), buffer.size(), 0);
        send(client, "\r\n", 2, 0);

        usleep(16000); // ~60fps
    }

    close(client);
    close(server_fd);
}
