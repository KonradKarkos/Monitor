#ifndef MONITOR_H
#define MONITOR_H
#include <iostream>
#include <string>
#include <map>
#include <zmq.h>
#include <Windows.h>
#include <sstream>
#include <fstream>
#include <mutex>
#include <vector>
#include <thread>
#include <list>
enum class process_state
{
	WAITING,
	WORKING,
	IDLE,
	REQUESTING
};


class monitor
{
private:
	static std::map<std::string, std::map<std::string, process_state>> waitingThreads;
	static std::mutex mtx;
	static void* context;
	static void* sckt;
	static std::string ownAddress;
	static int ownPort;
	static std::vector<std::string> allProcessesAddresses;
	std::list<std::string> currentRequestedOrTakenObjects;
	process_state currentState;
	std::thread checker;
	bool infinite;
	void check_for_more_ports();
	void send_all_message(std::string objectName, std::string msg, int addPulseMode);
	void add_process_address(std::string address);
	void add_process(std::string objectName, std::string processAddress, process_state state);
	void remove_process(std::string objectName, std::string processAddress);
	bool request_permission_to_enter(std::string objectName);
	std::vector<std::string> split(const std::string& s);
public:
	process_state get_current_process_state();
	void enter(std::string objectName, bool& lockAcquired);
	void try_enter(std::string objectName, bool& lockAcquired);
	void wait(std::string objectName, int miliseconds, bool& lockAcquired);
	void wait(std::string objectName, bool& lockAcquired);
	void pulse(std::string objectName);
	void pulse_all(std::string objectName);
	void exit(std::string objectName);
	void stop_checker_process();
	void check_for_other_processes();
	monitor();
	monitor(char* localPort);
	~monitor();
};
#endif
