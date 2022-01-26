#ifndef gnss_zed_f9p
#define gnss_zed_f9p

#include <fstream>
#include <iostream>
#include <cstdio>
#include <chrono>
#include <ctime> 
#include <string>
#include <chrono>
#include <thread>
#include <std_msgs/String.h>
#include <sensor_msgs/NavSatFix.h>


#include <fcntl.h>   // Contains file controls like O_RDWR
#include <errno.h>   // Error integer and strerror() function
#include <termios.h> // Contains POSIX terminal control definitions
#include <unistd.h>  // write(), read(), close()

#include "ros/ros.h"
#include "nav_msgs/Odometry.h"

#pragma pack(1)
typedef struct {
	char     magic[2];
	uint8_t  msgClass;
	uint8_t  id;
	uint16_t length; //little endian

	uint8_t  classId;
	uint8_t  messageId;

	uint8_t  ck_a;
	uint8_t  ck_b;
} ubx_ack;
#pragma pack()

#pragma pack(1)
typedef struct{
	char     magic[2];
	uint8_t  msgClass;
	uint8_t  id;
	uint16_t length; //little-endian

	uint8_t  version;
	uint8_t  layers;
	uint8_t  reserved[2];
	uint32_t key; //little-endian
	uint8_t  value;

        uint8_t  ck_a;
        uint8_t  ck_b;
} ubx_config;
#pragma pack()

#pragma pack(1)
typedef struct {
        char     magic[2];
        uint8_t  msgClass;
        uint8_t  id;
        uint16_t length; //little endian

        uint16_t navBbrMask; //little endian
        uint8_t  resetMode;
		uint8_t  reserved;

        uint8_t  ck_a;
        uint8_t  ck_b;
} ubx_rst;
#pragma pack()

#pragma pack(1)
typedef struct{
	uint8_t msgClass;
	uint8_t  id;
    uint16_t length; //little endian
}ubx_header;
#pragma pack()

#pragma pack(1)
typedef struct{
    uint8_t  ck_a;
    uint8_t  ck_b;
}ubx_checksum;
#pragma pack()

#pragma pack(1)
typedef struct{
	uint32_t iTOW;
	uint16_t year;
	uint8_t  month;
	uint8_t day;
	uint8_t hour;
	uint8_t min;
	uint8_t sec;
	uint8_t valid;
	uint32_t timeAccuracy;
	int32_t nanoSec;
	uint8_t fixType;
	uint8_t flags;
	uint8_t flags2;
	uint8_t nbSatellites;
	int32_t longitude; //1e-7 deg
	int32_t latitude; //1e-7;
	int32_t height; //milimeters , above ellipsoid
	int32_t heightMSL; //mm, above sea level
	uint32_t horizontalAccuracy; //mm
	uint32_t verticalAccuracy; //mm
	int32_t velocityNorth; //mm/s
	int32_t velocityEast; //mm/s
	int32_t velocityDown; //mm/s
	int32_t groundSpeed; //mm/s
	int32_t headingMotion; //1e-5 deg
	uint32_t speedAccuracy; //mm/s
	uint32_t headingAccuracy; //1e-5 deg
	uint16_t positionDOP; //0.01
	uint8_t flags3;
	uint8_t reserved[5];
	int32_t headingVessel; //1e-5 deg
	int16_t magneticDeclination; //1e-2 dec
	uint16_t magneticDeclinationAccuracy; //1e-2 deg
	
}ubx_nav_pvt;
#pragma pack()

class ZEDF9P{
	private:
	 	std::string outputFolder;
		std::string serialport;

		std_msgs::String result;
		std_msgs::String ubx_msg_str;
		std_msgs::String ubx_read;
		
		std::ofstream file; //binary log file
		int speedSequenceNumber = 1;
		
		bool bootstrappedGnssTime = false;

                ros::NodeHandle n;

                ros::Subscriber gnssSubscriber;
                ros::Publisher speedPublisher;

	public:

		ZEDF9P(std::string & outputFolder, std::string & serialport): outputFolder(outputFolder), serialport(serialport){
			gnssSubscriber = n.subscribe("fix", 1000, &ZEDF9P::gnssCallback,this);
			speedPublisher = n.advertise<nav_msgs::Odometry>("speed",1000);		
		}

		std::string datetime(){
    			time_t rawtime;
    			struct tm * timeinfo;
	    		char buffer[80];

    			time (&rawtime);
    			timeinfo = localtime(&rawtime);

   	 		strftime(buffer,80,"%Y.%m.%d_%H%M%S",timeinfo);
    			return std::string(buffer);
		}

		void gnssCallback(const sensor_msgs::NavSatFix& fix){
			if(!bootstrappedGnssTime && fix.status.status >= 0){
				bootstrappedGnssTime = true;
			}
		}
		bool validateChecksum(ubx_header *hdr, uint8_t *payload, ubx_checksum *checksum){
			uint8_t ck_a = 0;
			uint8_t ck_b = 0;
			for( unsigned int i = 0; i<sizeof(ubx_header);i++){
				ck_a += ((uint8_t*) hdr)[i];
				ck_b += ck_a; 
			}
			for( unsigned int i = 0; i<sizeof(hdr->length);i++){
				ck_a += payload[i];
				ck_b += ck_a; 
			}
			
			return checksum->ck_a == ck_a && checksum->ck_b == ck_b;
		}
		void processFrame(ubx_header *hdr, uint8_t *payload, ubx_checksum *checksum){
			//TODO verify checksum
			//TODO process , hdr
			
			if(validateChecksum(hdr, payload, checksum)){
				//UBX-NAV-PVT
				if(hdr->msgClass == 0x01 && hdr->id ==0x07){
					//extract ground speed and publish it
					ubx_nav_pvt* pvt = (ubx_nav_pvt*) payload;
					double speedKmh = (double) pvt->groundSpeed * (3.6/1000.0);
					nav_msgs::Odometry msg;
					msg.header.seq=++speedSequenceNumber;
					msg.header.stamp=ros::Time::now();
					msg.twist.twist.linear.y= speedKmh;
					speedPublisher.publish(msg);
					
				}
				
				
				
				//write frame to bin file
				uint16_t sync = 0xb562;
				file.write((char*)&sync, sizeof(uint16_t));
				file.write((char*)hdr, sizeof(ubx_header));
				file.write((char*)payload, hdr->length);
				file.write((char*)checksum, sizeof(ubx_checksum));
			}
			else{
				ROS_ERROR("zf9p checksum error");
			}
			
		}
		
		void run(){
			//serial port opening
			int serial_port = open(serialport.c_str(), O_RDWR);// | O_NOCTTY);
			struct termios tty;
			memset(&tty, 0, sizeof tty);
			//serialport config
			if(tcgetattr(serial_port, &tty) != 0) {
    				printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
			}

			tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
 			tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
			tty.c_cflag |= CS8; // 8 bits per byte (most common)
			tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
			tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

			tty.c_lflag &= ~ICANON;
			tty.c_lflag &= ~ECHO; // Disable echo
			tty.c_lflag &= ~ECHOE; // Disable erasure
			tty.c_lflag &= ~ECHONL; // Disable new-line echo
			tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
			tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
			tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

			tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
			tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
			// tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
			// tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

			tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
			tty.c_cc[VMIN] = 0;

			// Set in/out baud rate to be 9600
			cfsetispeed(&tty, B460800);
			cfsetospeed(&tty, B460800);

			if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
				printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
			}

			//Init buffer
			int size = 1;
			char read_buf [size];

			//Wait for GNSS fix to init system time
			while(!bootstrappedGnssTime){
				sleep(1);
				ros::spinOnce();
			}

			//open file
			std::string outputFilename = outputFolder + std::string(datetime()) + std::string(".ubx");
			file.open(outputFilename.c_str(),std::ios::app|std::ios::out|std::ios::binary);

			if(file) {
		                while(ros::ok()){ //read serial port and save in the file
		                /*
       		                int n = read(serial_port, &read_buf, size);
							file.write(read_buf, size);
						*/
						
							//read sync characters
							int n = read(serial_port, &read_buf, size);
							if (n == 1){
								if (read_buf[0] == 0xb5){
									n = read(serial_port, &read_buf, size);
									if(n == 1){
										if (read_buf[0] == 0x62){
										
											//read header
											ubx_header hdr;
											n = read(serial_port, &hdr, sizeof(ubx_header));
											if( n == sizeof(ubx_header)){
											
												//read payload
												uint8_t *payload = (uint8_t*) malloc(hdr.length);
												n = read(serial_port, payload, hdr.length);
												if(n == hdr.length){
													
													//read checksum
													ubx_checksum checksum;
													n = read(serial_port, &checksum, sizeof(ubx_checksum));
													if(n == sizeof(ubx_checksum)){
														
														//process frame
														processFrame(&hdr, payload, &checksum);
													}
													else{//read error
													}
												}
												else{//read error
												}
											}
											else{//read error
											}
										}
										else{//read error
										}
									}
									else{
										//read error , discard packet
									}
					        	}
					        	else{//read error
					        	}
					        }
					        else{
					        	//read error
					        }
						}
				//cleanup
		                close(serial_port);
	        	        file.close();
			}
			else{
				ROS_INFO("Cannot open UBX file\n");
				exit(1);
			}
		}

};


#endif
