#ifndef logger_text
#define logger_text
#include "loggerBase.h"
#include "types.h"

class LoggerBinary : public LoggerBase{
	public:
		LoggerBinary(std::string & loggingPath);
		~LoggerBinary();
		
		/* log management methods */
		void init();
		void finalize();
		void rotate();
		void updateLoggingMode();
		
		/* log transfer */
		bool compress(std::string &zipFilename, std::string &binSensorFileName, std::string &rawGnssFileName);
		
		/* topic callbacks */
		void gnssCallback(const sensor_msgs::NavSatFix& gnss);
		void imuCallback(const sensor_msgs::Imu& imu);
		void sonarCallback(const geometry_msgs::PointStamped& sonar);
		void speedCallback(const nav_msgs::Odometry& speed);
		void lidarCallBack(const sensor_msgs::PointCloud2& lidar);
		void gnssBinStreamCallback(const binary_stream_msg::Stream& stream);
		
	
	private:
				
		// log files
		//std::string  outputFilePath;
        std::string  outputFileName;
        std::ofstream outputFile;
        std::mutex fileLock;
        

};
#endif
