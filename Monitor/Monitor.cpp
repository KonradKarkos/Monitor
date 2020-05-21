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
using namespace std;
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
	static map<string, map<string, process_state>> waitingThreads;
	static mutex mtx;
	static void* context;
	static void* sckt;
	static string ownAddress;
	static int ownPort;
	static vector<string> allProcessesAddresses;
	list<string> currentRequestedOrTakenObjects;
	process_state currentState;
	thread checker;
	bool infinite;
	void check_for_more_ports()
	{
		ifstream fports;
		fports.open("ports.txt");
		if (fports.is_open())
		{
			string line;
			int i = 0;
			bool alreadyThere = false;
			while (getline(fports, line))
			{
				if (line == to_string(ownPort))
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
				ofstream ofports;
				ofports.open("ports.txt", ios::app);
				if (ofports.is_open())
				{
					ofports << ownPort << endl;
					ofports.flush();
				}
				ofports.close();
			}
		}
		else
		{
			fports.close();
			ofstream ofports;
			ofports.open("ports.txt");
			if (ofports.is_open())
			{
				ofports << to_string(ownPort) + "\n";
				ofports.flush();
			}
			ofports.close();
		}
	}
	void send_all_message(string objectName, string msg, int addPulseMode)
	{
		string addr;
		void* scktM = zmq_socket(context, ZMQ_REQ);
		int timeout = 1000;
		zmq_setsockopt(scktM, ZMQ_RCVTIMEO, &timeout, sizeof(int));
		vector<string> allProcessesAddressesTMP = allProcessesAddresses;
		char* bufMsg = new char[256];
		int newPort;
		string toSendMsg;
		for (int i = 0; i < allProcessesAddressesTMP.size(); i++)
		{
			addr = allProcessesAddressesTMP[i];
			newPort = stoi(addr.substr(addr.size() - 4, 4));
			newPort += addPulseMode;
			addr.replace(addr.size() - 4, 4, to_string(newPort));
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
	void add_process_address(string s)
	{
		allProcessesAddresses.push_back(s);
	}
	void add_process(string objectName, string processAddress, process_state state)
	{
		while (mtx.try_lock() == false) {}
		map<string, map<string, process_state>> waitingThreadsTmp = waitingThreads;
		map<string, map<string, process_state>>::iterator it = waitingThreadsTmp.find(objectName);
		if (it == waitingThreadsTmp.end())
		{
			map<string, process_state> m;
			m.insert(pair<string, process_state>(processAddress, state));
			waitingThreads.insert(pair<string, map<string, process_state>>(objectName, m));
		}
		else if (it != waitingThreadsTmp.end() && it->second.find(processAddress) == it->second.end())
		{
			waitingThreads.find(objectName)->second.insert(pair<string, process_state>(processAddress, state));
		}
		else if (it != waitingThreadsTmp.end() && it->second.find(processAddress) != it->second.end())
		{
			waitingThreads.find(objectName)->second.find(processAddress)->second = state;
		}
		mtx.unlock();
	}
	void remove_process(string objectName, string processAddress)
	{
		while (mtx.try_lock() == false) {}
		map<string, map<string, process_state>>::iterator it = waitingThreads.find(objectName);
		if (it != waitingThreads.end() && it->second.find(processAddress) != it->second.end())
		{
			it->second.erase(it->second.find(processAddress));
		}
		mtx.unlock();
	}
	bool request_permission_to_enter(string objectName)
	{
		bool permIssued = true;
		vector<string> allProcessesAddressesTMP = allProcessesAddresses;
		int length = allProcessesAddressesTMP.size();
		void* scktR = zmq_socket(context, ZMQ_REQ);
		char* bufMsg = new char[256];
		int recvMsgLen;
		string finalReply;
		int timeout = 2000;
		zmq_setsockopt(scktR, ZMQ_RCVTIMEO, &timeout, sizeof(int));
		string toSendMsg = "perment," + ownAddress + "," + objectName;
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
	vector<string> split(const string& s)
	{
		vector<string> tokens;
		string token;
		istringstream tokenStream(s);
		while (getline(tokenStream, token, ','))
		{
			tokens.push_back(token);
		}
		return tokens;
	}
public:
	process_state get_current_process_state()
	{
		return currentState;
	}
	void enter(string objectName, bool& lockAcquired)
	{
		currentState = process_state::REQUESTING;
		currentRequestedOrTakenObjects.push_back(objectName);
		lockAcquired = false;
		while (!lockAcquired)
		{
			while (mtx.try_lock() == false) {}
			map<string, map<string, process_state>>::iterator it = waitingThreads.find(objectName);
			if (it == waitingThreads.end())
			{
				if (request_permission_to_enter(objectName))
				{
					map<string, process_state> m;
					m.insert(pair<string, process_state>(ownAddress, process_state::WORKING));
					waitingThreads.insert(pair<string, map<string, process_state>>(objectName, m));
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
				for (map<string, process_state>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
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
							it->second.insert(pair<string, process_state>(ownAddress, process_state::WORKING));
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
	void try_enter(string objectName, bool& lockAcquired)
	{
		currentState = process_state::REQUESTING;
		currentRequestedOrTakenObjects.push_back(objectName);
		while (mtx.try_lock() == false) {}
		map<string, map<string, process_state>>::iterator it = waitingThreads.find(objectName);
		if (it == waitingThreads.end())
		{
			if (request_permission_to_enter(objectName))
			{
				map<string, process_state> m;
				m.insert(pair<string, process_state>(ownAddress, process_state::WORKING));
				waitingThreads.insert(pair<string, map<string, process_state>>(objectName, m));
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
			for (map<string, process_state>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
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
						it->second.insert(pair<string, process_state>(ownAddress, process_state::WORKING));
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
	void wait(string objectName, int miliseconds, bool& lockAcquired)
	{
		ostringstream ss;
		ss << "tcp://127.0.0.1:" << ownPort + 1;
		void* scktW = zmq_socket(context, ZMQ_REP);
		int timeout = 12;
		zmq_setsockopt(scktW, ZMQ_RCVTIMEO, &timeout, sizeof(int));
		zmq_bind(scktW, ss.str().c_str());
		map<string, map<string, process_state>> waitingThreadsTmp = waitingThreads;
		map<string, map<string, process_state>>::iterator it = waitingThreadsTmp.find(objectName);
		char* bufMsg = new char[256];
		string msg;
		vector<string> receivedMsgs;
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
				waitingThreads.find(objectName)->second.insert(pair<string, process_state>(ownAddress, process_state::WAITING));
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
	void wait(string objectName, bool& lockAcquired)
	{
		ostringstream ss;
		char* bufMsg = new char[256];
		int msgLen;
		string msg;
		int intervalCheck = 100;
		ss << "tcp://127.0.0.1:" << ownPort + 1;
		void* scktW = zmq_socket(context, ZMQ_REP);
		int timeout = 12;
		zmq_setsockopt(scktW, ZMQ_RCVTIMEO, &timeout, sizeof(int));
		zmq_bind(scktW, ss.str().c_str());
		map<string, map<string, process_state>> waitingThreadsTmp = waitingThreads;
		map<string, map<string, process_state>>::iterator it = waitingThreadsTmp.find(objectName);
		vector<string> receivedMsgs;
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
				waitingThreads.find(objectName)->second.insert(pair<string, process_state>(ownAddress, process_state::WAITING));
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
	void pulse(string objectName)
	{
		if (waitingThreads.find(objectName) != waitingThreads.end())
		{
			for (map<string, process_state>::iterator it2 = waitingThreads.find(objectName)->second.begin(); it2 != waitingThreads.find(objectName)->second.end(); it2++)
			{
				if (it2->second == process_state::WAITING)
				{
					string addr = it2->first;
					int newPort = stoi(addr.substr(addr.size() - 4, 4));
					newPort += 1;
					addr.replace(addr.size() - 4, 4, to_string(newPort));
					void* scktP = zmq_socket(context, ZMQ_REQ);
					char* bufMsg = new char[256];
					int timeout = 1000;
					string toSendMsg = "release," + ownAddress + "," + objectName;
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
	void pulse_all(string objectName)
	{
		if (waitingThreads.find(objectName) != waitingThreads.end())
		{
			send_all_message(objectName, "release", 1);
		}
	}
	void exit(string objectName)
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
	void stop_checker_process()
	{
		infinite = false;
	}
	void check_for_other_processes()
	{
		char* bufMsg = new char[256];
		int msgLen;
		string msg;
		vector<string> receivedMsgs;
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
						string replyMsg = "ok";
						cout << receivedMsgs[1] << " has requested to enter on " << receivedMsgs[2] << endl;
						map<string, map<string, process_state>>::iterator it = waitingThreads.find(receivedMsgs[2]);
						if (find(currentRequestedOrTakenObjects.begin(), currentRequestedOrTakenObjects.end(), receivedMsgs[2]) != currentRequestedOrTakenObjects.end())
						{
							replyMsg = "no";
						}
						if (it != waitingThreads.end() && replyMsg != "no")
						{
							for (map<string, process_state>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
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
							cout << receivedMsgs[1] << " is waiting on " << receivedMsgs[2] << endl;
							add_process(receivedMsgs[2], receivedMsgs[1], process_state::WAITING);
						}
						else if (receivedMsgs[0] == "entered")
						{
							cout << receivedMsgs[1] << " has enetred " << receivedMsgs[2] << endl;
							add_process(receivedMsgs[2], receivedMsgs[1], process_state::WORKING);
						}
						else if (receivedMsgs[0] == "release")
						{
							cout << receivedMsgs[1] << " has released " << receivedMsgs[2] << endl;
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
	monitor() {
		context = zmq_ctx_new();
		infinite = false;
		currentState = process_state::IDLE;
	}
	monitor(char* localPort)
	{
		ownPort = atoi(localPort);
		cout << "My port is " << ownPort << endl;
		context = zmq_ctx_new();
		sckt = zmq_socket(context, ZMQ_REP);
		ostringstream ss;
		ss << "tcp://127.0.0.1:" << localPort;
		ownAddress = ss.str();
		zmq_bind(sckt, ss.str().c_str());
		infinite = true;
		cout << "My ZMQ address is " << ownAddress << endl;
		checker = thread(&monitor::check_for_other_processes, this);
		checker.detach();
	}
	~monitor()
	{
		infinite = false;
		zmq_ctx_destroy(context);
		zmq_close(sckt);
	}
};
map<string, map<string, process_state>> monitor::waitingThreads{ waitingThreads };
vector<string> monitor::allProcessesAddresses;
int monitor::ownPort;
mutex monitor::mtx;
string monitor::ownAddress;
void* monitor::context;
void* monitor::sckt;

int main(int argc, char* argv[])
{
	//int mode = atoi(argv[2]);
	//char* port = argv[1];
	int mode = 2;
	char* port = (char*)"2000";
	monitor m(port);
	string objectName = "memoryAddressOrFileNameOrWhatever";
	bool lockAcquired = false;
	cout << "My mode is " << mode << endl;
	switch (mode)
	{
	case 1:
		Sleep(10000);
		cout << "1 entered sequence" << endl;
		m.try_enter(objectName, lockAcquired);
		if (lockAcquired)
			cout << "1 entered object" << endl;
		Sleep(10000);
		cout << "1 begins to wait on object" << endl;
		m.wait(objectName, lockAcquired);
		cout << "1 got object" << endl;
		Sleep(3000);
		m.exit(objectName);
		cout << "1 exited object" << endl;
		m.stop_checker_process();
		break;
	case 2:
		Sleep(10000);
		cout << "2 entered sequence" << endl;
		m.wait(objectName, 2000, lockAcquired);
		if (lockAcquired)
		{
			cout << "2 entered object" << endl;
		}
		else
		{
			cout << "2 waited 2 seconds on object and failed" << endl;
		}
		Sleep(10000);
		cout << "2 begins to wait object" << endl;
		m.wait(objectName, lockAcquired);
		cout << "2 got object" << endl;
		m.exit(objectName);
		cout << "2 exited object" << endl;
		m.stop_checker_process();
		break;
	case 3:
		Sleep(10000);
		cout << "3 entered sequence" << endl;
		m.try_enter(objectName, lockAcquired);
		if (lockAcquired)
		{
			cout << "3 entered object" << endl;
		}
		else
		{
			cout << "3 begins to wait object" << endl;
			m.wait(objectName, lockAcquired);
			cout << "3 got object" << endl;
		}
		m.exit(objectName);
		cout << "3 exited object" << endl;
		m.stop_checker_process();
		break;
	default:
		break;
	}
	return 0;
}