# geek_szitman_supercamera_cpp
this repository contain geek_szitman_supercamera (0329:2022) frame extraction

## Environment
- OS : Ubuntu 22.04
- USB Camera Vendor ID : 0329
- USB Camera Product ID : 2022
   (0329:2022)
- image size : 640x480
- image format : jpeg

## How to use
'''bash
mkdir build && cd build && cmake .. && make
./build/camera_app
'''

'camera_app' will need 'sudo' if you don't configure your camera permission
