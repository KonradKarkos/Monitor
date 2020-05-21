#include "monitor.h"
std::map<std::string, std::map<std::string, process_state>> monitor::waitingThreads;
std::mutex monitor::mtx;
void* monitor::context;
void* monitor::sckt;
std::string monitor::ownAddress;
int monitor::ownPort;
std::vector<std::string> monitor::allProcessesAddresses;
void monitor::check_for_more_ports()
{
	std::ifstream fports;
	fports.open("ports.txt");
	if (fports.is_open())
	{
		std::string line;
		int i = 0;
		bool alreadyThere = false;
		while (getline(fports, line))
		{
			if (line == std::to_string(ownPort))
			{
				alreadyThere = true;
			}
			else
			{
				add_process_address("tcp://127.0.0.1:" + line);
			}
		}
		fports.close();
		if (!alreadyThere)
		{
			std::ofstream ofports;
			ofports.open("ports.txt", std::ios::app);
			if (ofports.is_open())
			{
				ofports << ownPort << std::endl;
				ofports.flush();
			}
			ofports.close();
		}
	}
	else
	{
		fports.close();
		std::ofstream ofports;
		ofports.open("ports.txt");
		if (ofports.is_open())
		{
			ofports << std::to_string(ownPort) + "\n";
			ofports.flush();
		}
		ofports.close();
	}
}
void monitor::send_all_message(std::string objectName, std::string msg, int addPulseMode)
{
	std::string addr;
	void* scktM = zmq_socket(context, ZMQ_REQ);
	int timeout = 1000;
	zmq_setsockopt(scktM, ZMQ_RCVTIMEO, &timeout, sizeof(int));
	std::vector<std::string> allProcessesAddressesTMP = allProcessesAddresses;
	char* bufMsg = new char[256];
	int newPort;
	std::string toSendMsg;
	for (int i = 0; i < allProcessesAddressesTMP.size(); i++)
	{
		addr = allProcessesAddressesTMP[i];
		newPort = stoi(addr.substr(addr.size() - 4, 4));
		newPort += addPulseMode;
		addr.replace(addr.size() - 4, 4, std::to_string(newPort));
		toSendMsg = msg + "," + ownAddress + "," + objectName;
		if (zmq_connect(scktM, addr.c_str()) == 0)
		{
			if (zmq_send(scktM, toSendMsg.c_str(), strlen(toSendMsg.c_str()), 0) > -1)
				zmq_recv(scktM, bufMsg, 256, 0);
			zmq_disconnect(scktM, addr.c_str());
		}
	}
	zmq_close(scktM);
	delete[] bufMsg;
}
void monitor::add_process_address(std::string address)
{
	allProcessesAddresses.push_back(address);
}
void monitor::add_process(std::string objectName, std::string processAddress, process_state state)
{
	while (mtx.try_lock() == false) {}
	std::map<std::string, std::map<std::string, process_state>> waitingThreadsTmp = waitingThreads;
	std::map<std::string, std::map<std::string, process_state>>::iterator it = waitingThreadsTmp.find(objectName);
	if (it == waitingThreadsTmp.end())
	{
		std::map<std::string, process_state> m;
		m.insert(std::pair<std::string, process_state>(processAddress, state));
		waitingThreads.insert(std::pair<std::string, std::map<std::string, process_state>>(objectName, m));
	}
	else if (it != waitingThreadsTmp.end() && it->second.find(processAddress) == it->second.end())
	{
		waitingThreads.find(objectName)->second.insert(std::pair<std::string, process_state>(processAddress, state));
	}
	else if (it != waitingThreadsTmp.end() && it->second.find(processAddress) != it->second.end())
	{
		waitingThreads.find(objectName)->second.find(processAddress)->second = state;
	}
	mtx.unlock();
}
void monitor::remove_process(std::string objectName, std::string processAddress)
{
	while (mtx.try_lock() == false) {}
	std::map<std::string, std::map<std::string, process_state>>::iterator it = waitingThreads.find(objectName);
	if (it != waitingThreads.end() && it->second.find(processAddress) != it->second.end())
	{
		it->second.erase(it->second.find(processAddress));
	}
	mtx.unlock();
}
bool monitor::request_permission_to_enter(std::string objectName)
{
	bool permIssued = true;
	std::vector<std::string> allProcessesAddressesTMP = allProcessesAddresses;
	int length = allProcessesAddressesTMP.size();
	void* scktR = zmq_socket(context, ZMQ_REQ);
	char* bufMsg = new char[256];
	int recvMsgLen;
	std::string finalReply;
	int timeout = 2000;
	zmq_setsockopt(scktR, ZMQ_RCVTIMEO, &timeout, sizeof(int));
	std::string toSendMsg = "perment," + ownAddress + "," + objectName;
	for (int i = 0; i < allProcessesAddressesTMP.size(); i++)
	{
		if (zmq_connect(scktR, allProcessesAddressesTMP[i].c_str()) == 0)
		{
			if (zmq_send(scktR, toSendMsg.c_str(), strlen(toSendMsg.c_str()), 0) > -1)
			{
				if ((recvMsgLen = zmq_recv(scktR, bufMsg, 256, 0)) > -1)
				{
					finalReply.assign(bufMsg, recvMsgLen);
					if (finalReply == "no" || finalReply == "fail")
					{
						permIssued = false;
					}
				}
			}
			zmq_disconnect(scktR, allProcessesAddressesTMP[i].c_str());
		}
	}
	zmq_close(scktR);
	delete[] bufMsg;
	return permIssued;
}
std::vector<std::string> monitor::split(const std::string& s)
{
	std::vector<std::string> tokens;
	std::string token;
	std::istringstream tokenStream(s);
	while (getline(tokenStream, token, ','))
	{
		tokens.push_back(token);
	}
	return tokens;
}
process_state monitor::get_current_process_state()
{
	return currentState;
}
void monitor::enter(std::string objectName, bool& lockAcquired)
{
	currentState = process_state::REQUESTING;
	currentRequestedOrTakenObjects.push_back(objectName);
	lockAcquired = false;
	while (!lockAcquired)
	{
		while (mtx.try_lock() == false) {}
		std::map<std::string, std::map<std::string, process_state>>::iterator it = waitingThreads.find(objectName);
		if (it == waitingThreads.end())
		{
			if (request_permission_to_enter(objectName))
			{
				std::map<std::string, process_state> m;
				m.insert(std::pair<std::string, process_state>(ownAddress, process_state::WORKING));
				waitingThreads.insert(std::pair<std::string, std::map<std::string, process_state>>(objectName, m));
				lockAcquired = true;
				send_all_message(objectName, "entered", 0);
			}
			else
			{
				lockAcquired = false;
				currentRequestedOrTakenObjects.remove(objectName);
			}
		}
		else
		{
			bool isTaken = false;
			for (std::map<std::string, process_state>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
			{
				if (it2->second == process_state::WORKING)
				{
					isTaken = true;
					lockAcquired = false;
					currentRequestedOrTakenObjects.remove(objectName);
					break;
				}
			}
			if (!isTaken)
			{
				if (request_permission_to_enter(objectName))
				{
					if (it->second.find(ownAddress) == it->second.end())
					{
						it->second.insert(std::pair<std::string, process_state>(ownAddress, process_state::WORKING));
					}
					else
					{
						it->second.find(ownAddress)->second = process_state::WORKING;
					}
					send_all_message(objectName, "entered", 0);
					lockAcquired = true;
				}
				else
				{
					lockAcquired = false;
					currentRequestedOrTakenObjects.remove(objectName);
				}
			}
		}
		mtx.unlock();
		Sleep(30);
	}
	currentState = process_state::WORKING;
}
void monitor::try_enter(std::string objectName, bool& lockAcquired)
{
	currentState = process_state::REQUESTING;
	currentRequestedOrTakenObjects.push_back(objectName);
	while (mtx.try_lock() == false) {}
	std::map<std::string, std::map<std::string, process_state>>::iterator it = waitingThreads.find(objectName);
	if (it == waitingThreads.end())
	{
		if (request_permission_to_enter(objectName))
		{
			std::map<std::string, process_state> m;
			m.insert(std::pair<std::string, process_state>(ownAddress, process_state::WORKING));
			waitingThreads.insert(std::pair<std::string, std::map<std::string, process_state>>(objectName, m));
			lockAcquired = true;
			send_all_message(objectName, "entered", 0);
			currentState = process_state::WORKING;
		}
		else
		{
			lockAcquired = false;
			currentRequestedOrTakenObjects.remove(objectName);
		}
	}
	else
	{
		bool isTaken = false;
		for (std::map<std::string, process_state>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
		{
			if (it2->second == process_state::WORKING)
			{
				isTaken = true;
				lockAcquired = false;
				currentRequestedOrTakenObjects.remove(objectName);
				break;
			}
		}
		if (!isTaken)
		{
			if (request_permission_to_enter(objectName))
			{
				if (it->second.find(ownAddress) == it->second.end())
				{
					it->second.insert(std::pair<std::string, process_state>(ownAddress, process_state::WORKING));
				}
				else
				{
					it->second.find(ownAddress)->second = process_state::WORKING;
				}
				currentState = process_state::WORKING;
				send_all_message(objectName, "entered", 0);
				lockAcquired = true;
			}
			else
			{
				lockAcquired = false;
				currentRequestedOrTakenObjects.remove(objectName);
			}
		}
	}
	mtx.unlock();
}
void monitor::wait(std::string objectName, int miliseconds, bool& lockAcquired)
{
	std::ostringstream ss;
	ss << "tcp://127.0.0.1:" << ownPort + 1;
	void* scktW = zmq_socket(context, ZMQ_REP);
	int timeout = 12;
	zmq_setsockopt(scktW, ZMQ_RCVTIMEO, &timeout, sizeof(int));
	zmq_bind(scktW, ss.str().c_str());
	std::map<std::string, std::map<std::string, process_state>> waitingThreadsTmp = waitingThreads;
	std::map<std::string, std::map<std::string, process_state>>::iterator it = waitingThreadsTmp.find(objectName);
	char* bufMsg = new char[256];
	std::string msg;
	std::vector<std::string> receivedMsgs;
	int msgLen;
	if (it == waitingThreadsTmp.end())
	{
		zmq_close(scktW);
		try_enter(objectName, lockAcquired);
	}
	else
	{
		if (it->second.find(ownAddress) != it->second.end())
		{
			if (it->second.find(ownAddress)->second == process_state::WORKING)
			{
				currentRequestedOrTakenObjects.remove(objectName);
				pulse_all(objectName);
			}
			waitingThreads.find(objectName)->second.find(ownAddress)->second = process_state::WAITING;
			currentState = process_state::WAITING;
		}
		else
		{
			waitingThreads.find(objectName)->second.insert(std::pair<std::string, process_state>(ownAddress, process_state::WAITING));
			currentState = process_state::WAITING;
		}
		send_all_message(objectName, "waiting", 0);
		while (miliseconds > 0 && (receivedMsgs.size() == 0 || receivedMsgs[0] != "release"))
		{
			if ((msgLen = zmq_recv(scktW, bufMsg, 256, 0)) > -1)
			{
				zmq_send(scktW, "ok", 2, 0);
				msg.assign(bufMsg, msgLen);
				receivedMsgs = split(msg);
			}
			miliseconds -= timeout;
		}
		zmq_close(scktW);
		delete[] bufMsg;
		if (miliseconds <= 0)
		{
			try_enter(objectName, lockAcquired);
		}
		if (receivedMsgs.size() > 2 && receivedMsgs[0] == "release")
		{
			remove_process(receivedMsgs[2], receivedMsgs[1]);
			if (!lockAcquired)
			{
				try_enter(objectName, lockAcquired);
			}
		}
	}
	if (!lockAcquired && miliseconds > 0)
	{
		wait(objectName, miliseconds, lockAcquired);
	}
}
void monitor::wait(std::string objectName, bool& lockAcquired)
{
	std::ostringstream ss;
	char* bufMsg = new char[256];
	int msgLen;
	std::string msg;
	int intervalCheck = 100;
	ss << "tcp://127.0.0.1:" << ownPort + 1;
	void* scktW = zmq_socket(context, ZMQ_REP);
	int timeout = 12;
	zmq_setsockopt(scktW, ZMQ_RCVTIMEO, &timeout, sizeof(int));
	zmq_bind(scktW, ss.str().c_str());
	std::map<std::string, std::map<std::string, process_state>> waitingThreadsTmp = waitingThreads;
	std::map<std::string, std::map<std::string, process_state>>::iterator it = waitingThreadsTmp.find(objectName);
	std::vector<std::string> receivedMsgs;
	receivedMsgs.push_back("");
	if (it == waitingThreadsTmp.end())
	{
		zmq_close(scktW);
		enter(objectName, lockAcquired);
	}
	else
	{
		if (it->second.find(ownAddress) != it->second.end())
		{
			if (it->second.find(ownAddress)->second == process_state::WORKING)
			{
				currentRequestedOrTakenObjects.remove(objectName);
				pulse_all(objectName);
			}
			waitingThreads.find(objectName)->second.find(ownAddress)->second = process_state::WAITING;
			currentState = process_state::WAITING;
		}
		else
		{
			waitingThreads.find(objectName)->second.insert(std::pair<std::string, process_state>(ownAddress, process_state::WAITING));
			currentState = process_state::WAITING;
		}
		Sleep(15);
		send_all_message(objectName, "waiting", 0);
		while (!lockAcquired && (receivedMsgs.size() == 0 || receivedMsgs[0] != "release"))
		{
			if ((msgLen = zmq_recv(scktW, bufMsg, 256, 0)) > -1)
			{
				zmq_send(scktW, "ok", 2, 0);
				msg.assign(bufMsg, msgLen);
				receivedMsgs = split(msg);
			}
			intervalCheck--;
			if (intervalCheck == 0)
			{
				intervalCheck = 100;
				try_enter(objectName, lockAcquired);
			}
		}
		zmq_close(scktW);
		delete[] bufMsg;
		if (receivedMsgs.size() > 2 && receivedMsgs[0] == "release")
		{
			remove_process(receivedMsgs[2], receivedMsgs[1]);
			if (!lockAcquired)
			{
				try_enter(objectName, lockAcquired);
			}
		}
		if (!lockAcquired)
		{
			wait(objectName, lockAcquired);
		}
	}
}
void monitor::pulse(std::string objectName)
{
	if (waitingThreads.find(objectName) != waitingThreads.end())
	{
		for (std::map<std::string, process_state>::iterator it2 = waitingThreads.find(objectName)->second.begin(); it2 != waitingThreads.find(objectName)->second.end(); it2++)
		{
			if (it2->second == process_state::WAITING)
			{
				std::string addr = it2->first;
				int newPort = stoi(addr.substr(addr.size() - 4, 4));
				newPort += 1;
				addr.replace(addr.size() - 4, 4, std::to_string(newPort));
				void* scktP = zmq_socket(context, ZMQ_REQ);
				char* bufMsg = new char[256];
				int timeout = 1000;
				std::string toSendMsg = "release," + ownAddress + "," + objectName;
				zmq_setsockopt(scktP, ZMQ_RCVTIMEO, &timeout, sizeof(int));
				zmq_connect(scktP, addr.c_str());
				if (zmq_send(scktP, toSendMsg.c_str(), strlen(toSendMsg.c_str()), 0) > -1)
					zmq_recv(scktP, bufMsg, 256, 0);
				zmq_close(scktP);
				delete[] bufMsg;
				break;
			}
		}
	}
}
void monitor::pulse_all(std::string objectName)
{
	if (waitingThreads.find(objectName) != waitingThreads.end())
	{
		send_all_message(objectName, "release", 1);
	}
}
void monitor::exit(std::string objectName)
{
	if (find(currentRequestedOrTakenObjects.begin(), currentRequestedOrTakenObjects.end(), objectName) != currentRequestedOrTakenObjects.end())
	{
		currentRequestedOrTakenObjects.remove(objectName);
	}
	if (waitingThreads.find(objectName) != waitingThreads.end())
	{
		send_all_message(objectName, "release", 0);
		send_all_message(objectName, "release", 1);
	}
	currentState = process_state::IDLE;
}
void monitor::stop_checker_process()
{
	infinite = false;
}
void monitor::check_for_other_processes()
{
	char* bufMsg = new char[256];
	int msgLen;
	std::string msg;
	std::vector<std::string> receivedMsgs;
	while (infinite)
	{
		check_for_more_ports();
		if ((msgLen = zmq_recv(sckt, bufMsg, 256, 0)) > -1)
		{
			msg.assign(bufMsg, msgLen);
			receivedMsgs = split(msg);
			if (receivedMsgs.size() > 2)
			{
				if (receivedMsgs[0] == "perment")
				{
					std::string replyMsg = "ok";
					std::cout << receivedMsgs[1] << " has requested to enter " << receivedMsgs[2] << std::endl;
					std::map<std::string, std::map<std::string, process_state>>::iterator it = waitingThreads.find(receivedMsgs[2]);
					if (find(currentRequestedOrTakenObjects.begin(), currentRequestedOrTakenObjects.end(), receivedMsgs[2]) != currentRequestedOrTakenObjects.end())
					{
						replyMsg = "no";
					}
					if (it != waitingThreads.end() && replyMsg != "no")
					{
						for (std::map<std::string, process_state>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
						{
							if (it2->second == process_state::WORKING)
							{
								replyMsg = "no";
								break;
							}
						}
					}
					zmq_send(sckt, replyMsg.c_str(), 2, 0);
				}
				else
				{
					zmq_send(sckt, "ok", 2, 0);
					if (receivedMsgs[0] == "waiting")
					{
						std::cout << receivedMsgs[1] << " is waiting on " << receivedMsgs[2] << std::endl;
						add_process(receivedMsgs[2], receivedMsgs[1], process_state::WAITING);
					}
					else if (receivedMsgs[0] == "entered")
					{
						std::cout << receivedMsgs[1] << " has entered " << receivedMsgs[2] << std::endl;
						add_process(receivedMsgs[2], receivedMsgs[1], process_state::WORKING);
					}
					else if (receivedMsgs[0] == "release")
					{
						std::cout << receivedMsgs[1] << " has released " << receivedMsgs[2] << std::endl;
						remove_process(receivedMsgs[2], receivedMsgs[1]);
					}
				}
			}
			else
			{
				zmq_send(sckt, "fail", 4, 0);
			}
		}
		Sleep(10);
	}
	delete[] bufMsg;
}
monitor::monitor() {
	context = zmq_ctx_new();
	infinite = false;
	currentState = process_state::IDLE;
}
monitor::monitor(char* localPort)
{
	ownPort = atoi(localPort);
	std::cout << "My port is " << ownPort << std::endl;
	context = zmq_ctx_new();
	sckt = zmq_socket(context, ZMQ_REP);
	std::ostringstream ss;
	ss << "tcp://127.0.0.1:" << localPort;
	ownAddress = ss.str();
	zmq_bind(sckt, ss.str().c_str());
	infinite = true;
	std::cout << "My ZMQ address is " << ownAddress << std::endl;
	checker = std::thread(&monitor::check_for_other_processes, this);
	checker.detach();
}
monitor::~monitor()
{
	infinite = false;
	zmq_ctx_destroy(context);
	zmq_close(sckt);
}
