echo "|| Programa paleidžiama iš linux pusės ||"
g++ main.cpp -o cam \
$(pkg-config --cflags --libs opencv4) \
-pthread
./cam



