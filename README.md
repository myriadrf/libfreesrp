# libfreesrp

The [FreeSRP](http://electronics.kitchen/freesrp) is an open source platform for software defined radio. The hardware is based around the [Analog Devices AD9364](http://www.analog.com/en/products/rf-microwave/integrated-transceivers-transmitters-receivers/wideband-transceivers-ic/ad9364.html) transceiver covering 70 MHz - 6 Ghz with a bandwidth of up to 50 MHz, an Xilinx Artix 7 FPGA and a USB 3.0 connection to stream data to a computer in real time.

libfreesrp is a small C++ library that uses [libusb](http://www.libusb.org/) to program and configure the FreeSRP hardware and both receive and transmit RF signals.

## Under construction

Right now, programming the FreeSRP, configuring the AD9364 and receiving data works well. However, this library has not yet been thoroughly tested and transmitting is not yet possible.

## Getting started

libfreesrp depends on libusb 1.0 and some boost libraries, and uses CMake as its build system.

On Ubuntu 14.04, this is how to build and install the library and its dependencies:
```
# Install the dependencies
sudo apt-get install build-essential cmake libusb-1.0-0-dev libboost-all-dev

# Get the latest libfreesrp source code
git clone https://github.com/FreeSRP/libfreesrp.git

# Build the library
cd libfreesrp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# Install the library
sudo make install
```
