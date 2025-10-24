Setup terminal to use ESP-IDF tools: $HOME/esp-idf/export.sh
Build: idf.py build flash
Run: idf.py -p /dev/ttyACM0 monitor

Second Terminal (to move motor): echo '{"command":"move_x","amount":100}' | sudo tee /dev/ttyACM0
