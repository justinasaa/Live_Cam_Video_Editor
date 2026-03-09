echo 'stream veikia'
g++ translate.cpp -o translate `pkg-config --cflags --libs opencv4`
./translate
