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
using namespace std;
enum class State
{
    WAITING,
    WORKING
};


class Monitor
{
private:
    static map<string, map<string, State>> waitingThreads;
    static mutex mtx;
    static void* context;
    static void* sckt;
    static string ownAddress;
    static int ownPort;
    static vector<string> allProcessesAddresses;
    thread checker;
    bool infinite;
    void CheckForMorePorts()
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
                    AddProcessAddress("tcp://127.0.0.1:" + line);
                }
            }
            fports.close();
            if (!alreadyThere)
            {
                ofstream ofports;
                ofports.open("ports.txt", ios::app);
                ofports << ownPort << endl;
                ofports.flush();
                ofports.close();
            }
        }
        else
        {
            fports.close();
            ofstream ofports;
            ofports.open("ports.txt");
            ofports << to_string(ownPort) + "\n";
            ofports.flush();
            ofports.close();
        }
    }
    void SendAllMessage(string objectName, string msg, int addPulseMode)
    {
        string addr;
        void* scktM = zmq_socket(context, ZMQ_REQ);
        int timeout = 1000;
        zmq_setsockopt(scktM, ZMQ_RCVTIMEO, &timeout, sizeof(int));
        for (vector<string>::iterator it2 = allProcessesAddresses.begin(); it2 != allProcessesAddresses.end(); it2++)
        {
            addr = it2->data();
            int newPort = stoi(addr.substr(addr.size() - 4, 4));
            newPort += addPulseMode;
            char* bufMsg = new char[256];
            addr.replace(addr.size() - 4, 4, to_string(newPort));
            if (zmq_connect(scktM, addr.c_str()) == 0)
            {
                if (zmq_send(scktM, msg.c_str(), strlen(msg.c_str()), 0) > -1)
                    if (zmq_recv(scktM, bufMsg, 256, 0) > -1)
                        if (zmq_send(scktM, ownAddress.c_str(), strlen(ownAddress.c_str()), 0) > -1)
                            if (zmq_recv(scktM, bufMsg, 256, 0) > -1)
                                if (zmq_send(scktM, objectName.c_str(), strlen(objectName.c_str()), 0) > -1)
                                    zmq_recv(scktM, bufMsg, 256, 0);
                zmq_disconnect(scktM, addr.c_str());
            }
        }
        zmq_close(scktM);
    }
    void AddProcessAddress(string s)
    {
        allProcessesAddresses.push_back(s);
    }
    void AddProcess(string objectName, string processAddress, State state)
    {
        mtx.lock();
        static map<string, map<string, State>>::iterator it = waitingThreads.find(objectName);
        if (it == waitingThreads.end())
        {
            map<string, State> m;
            m.insert(pair<string, State>(processAddress, state));
            waitingThreads.insert(pair<string, map<string, State>>(objectName, m));
        }
        else if (it != waitingThreads.end() && it->second.find(processAddress) == it->second.end())
        {
            it->second.insert(pair<string, State>(processAddress, state));
        }
        else if (it != waitingThreads.end() && it->second.find(processAddress) != it->second.end())
        {
            it->second.find(processAddress)->second = state;
        }
        mtx.unlock();
    }
    void RemoveProcess(string objectName, string processAddress)
    {
        mtx.lock();
        static map<string, map<string, State>>::iterator it = waitingThreads.find(objectName);
        if (it != waitingThreads.end() && it->second.find(processAddress) != it->second.end())
        {
            it->second.erase(it->second.find(processAddress));
        }
        mtx.unlock();
    }
public:
    void Enter(string objectName, bool& lockAcquired)
    {
        mtx.lock();
        static map<string, map<string, State>>::iterator it = waitingThreads.find(objectName);
        if (it == waitingThreads.end())
        {
            map<string, State> m;
            m.insert(pair<string, State>(ownAddress, State::WORKING));
            waitingThreads.insert(pair<string, map<string, State>>(objectName, m));
            lockAcquired = true;
            SendAllMessage(objectName, "entered", 0);
        }
        else if (it != waitingThreads.end() && it->second.find(ownAddress) == it->second.end())
        {
            bool isTaken = false;
            for (map<string, State>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
            {
                if (it2->second == State::WORKING)
                {
                    isTaken = true;
                    lockAcquired = false;
                    break;
                }
            }
            if (!isTaken)
            {
                it->second.insert(pair<string, State>(ownAddress, State::WORKING));
                SendAllMessage(objectName, "entered", 0);
                lockAcquired = true;
            }
        }
        else if (it != waitingThreads.end() && it->second.find(ownAddress) != it->second.end())
        {
            bool isTaken = false;
            for (map<string, State>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
            {
                if (it2->second == State::WORKING)
                {
                    isTaken = true;
                    lockAcquired = false;
                    break;
                }
            }
            if (!isTaken)
            {
                it->second.find(ownAddress)->second = State::WORKING;
                SendAllMessage(objectName, "entered", 0);
                lockAcquired = true;
            }
        }
        else
        {
            lockAcquired = false;
        }
        mtx.unlock();
    }
    void Wait(string objectName, int miliseconds, bool& lockAcquired)
    {
        std::ostringstream ss;
        ss << "tcp://127.0.0.1:" << ownPort + 1;
        void* scktW = zmq_socket(context, ZMQ_REP);
        int timeout = 4;
        zmq_setsockopt(scktW, ZMQ_RCVTIMEO, &timeout, sizeof(int));
        zmq_bind(scktW, ss.str().c_str());
        static map<string, map<string, State>>::iterator it = waitingThreads.find(objectName);
        if (it == waitingThreads.end())
        {
            zmq_close(scktW);
            Enter(objectName, lockAcquired);
        }
        else if (it != waitingThreads.end() && it->second.find(ownAddress) != it->second.end())
        {
            it->second.find(ownAddress)->second = State::WAITING;
            SendAllMessage(objectName, "waiting", 0);
            char* bufMsg = new char[7];
            char* bufObj = new char[20];
            char* bufAddr = new char[256];
            int msgLen;
            int addrLen;
            int objNameLen;
            std::string msg;
            while (miliseconds > 0 && msg != "release")
            {
                if ((msgLen = zmq_recv(scktW, bufMsg, 7, 0)) > -1)
                    if (zmq_send(scktW, "ok", 2, 0) > -1)
                        if ((addrLen = zmq_recv(scktW, bufAddr, 20, 0)) > -1)
                            if (zmq_send(scktW, "ok", 2, 0) > -1)
                                if ((objNameLen = zmq_recv(scktW, bufObj, 256, 0)) > -1)
                                {
                                    zmq_send(scktW, "ok", 2, 0);
                                    msg.assign(bufMsg, msgLen);
                                }
                miliseconds -= 12;
            }
            zmq_close(scktW);
            if (msg == "release")
            {
                std::string objName(bufObj, objNameLen);
                std::string objPAddress(bufAddr, addrLen);
                RemoveProcess(objName, objPAddress);
                Enter(objectName, lockAcquired);
            }
            if (!lockAcquired && miliseconds > 0)
            {
                Wait(objectName, miliseconds, lockAcquired);
            }
        }
        else
        {
            it->second.insert(pair<string, State>(ownAddress, State::WAITING));
            SendAllMessage(objectName, "waiting", 0);
            char* bufMsg = new char[7];
            char* bufObj = new char[20];
            char* bufAddr = new char[256];
            int msgLen;
            int addrLen;
            int objNameLen;
            std::string msg;
            while (miliseconds > 0 && msg != "release")
            {
                if ((msgLen = zmq_recv(scktW, bufMsg, 7, 0)) > -1)
                    if (zmq_send(scktW, "ok", 2, 0) > -1)
                        if ((addrLen = zmq_recv(scktW, bufAddr, 20, 0)) > -1)
                            if (zmq_send(scktW, "ok", 2, 0) > -1)
                                if ((objNameLen = zmq_recv(scktW, bufObj, 256, 0)) > -1)
                                {
                                    zmq_send(scktW, "ok", 2, 0);
                                    msg.assign(bufMsg, msgLen);
                                }
                miliseconds -= 12;
            }
            if (miliseconds <= 0)
            {
                Enter(objectName, lockAcquired);
            }
            zmq_close(scktW);
            if (msg == "release")
            {
                std::string objName(bufObj, objNameLen);
                std::string objPAddress(bufAddr, addrLen);
                RemoveProcess(objName, objPAddress);
                if (!lockAcquired)
                {
                    Enter(objectName, lockAcquired);
                }
            }
            if (!lockAcquired && miliseconds > 0)
            {
                Wait(objectName, miliseconds, lockAcquired);
            }
        }
    }
    void Wait(string objectName, bool& lockAcquired)
    {
        std::ostringstream ss;
        ss << "tcp://127.0.0.1:" << ownPort + 1;
        void* scktW = zmq_socket(context, ZMQ_REP);
        int timeout = 20;
        zmq_setsockopt(scktW, ZMQ_RCVTIMEO, &timeout, sizeof(int));
        zmq_bind(scktW, ss.str().c_str());
        static map<string, map<string, State>>::iterator it = waitingThreads.find(objectName);
        if (it == waitingThreads.end())
        {
            zmq_close(scktW);
            Enter(objectName, lockAcquired);
        }
        else if (it != waitingThreads.end() && it->second.find(ownAddress) != it->second.end())
        {
            it->second.find(ownAddress)->second = State::WAITING;
            PulseAll(objectName);
            Sleep(15);
            SendAllMessage(objectName, "waiting", 0);
            char* bufMsg = new char[7];
            char* bufObj = new char[20];
            char* bufAddr = new char[256];
            int msgLen;
            int addrLen;
            int objNameLen;
            std::string msg;
            int intervalCheck = 100;
            while (msg != "release" && !lockAcquired)
            {
                if ((msgLen = zmq_recv(scktW, bufMsg, 7, 0)) > -1)
                    if (zmq_send(scktW, "ok", 2, 0) > -1)
                        if ((addrLen = zmq_recv(scktW, bufAddr, 20, 0)) > -1)
                            if (zmq_send(scktW, "ok", 2, 0) > -1)
                                if ((objNameLen = zmq_recv(scktW, bufObj, 256, 0)) > -1)
                                {
                                    zmq_send(scktW, "ok", 2, 0);
                                    msg.assign(bufMsg, msgLen);
                                }
                if (intervalCheck == 0)
                {
                    intervalCheck = 100;
                    Enter(objectName, lockAcquired);
                }
                intervalCheck--;
            }
            zmq_close(scktW);
            if (msg == "release")
            {
                std::string objName(bufObj, objNameLen);
                std::string objPAddress(bufAddr, addrLen);
                RemoveProcess(objName, objPAddress);
                Enter(objectName, lockAcquired);
            }
            if (!lockAcquired)
            {
                Wait(objectName, lockAcquired);
            }
        }
        else
        {
            it->second.insert(pair<string, State>(ownAddress, State::WAITING));
            SendAllMessage(objectName, "waiting", 0);
            char* bufMsg = new char[7];
            char* bufObj = new char[20];
            char* bufAddr = new char[256];
            int msgLen;
            int addrLen;
            int objNameLen;
            std::string msg;
            while (msg != "release")
            {
                if ((msgLen = zmq_recv(scktW, bufMsg, 7, 0)) > -1)
                    if (zmq_send(scktW, "ok", 2, 0) > -1)
                        if ((addrLen = zmq_recv(scktW, bufAddr, 20, 0)) > -1)
                            if (zmq_send(scktW, "ok", 2, 0) > -1)
                                if ((objNameLen = zmq_recv(scktW, bufObj, 256, 0)) > -1)
                                {
                                    zmq_send(scktW, "ok", 2, 0);
                                    msg.assign(bufMsg, msgLen);
                                }
            }
            zmq_close(scktW);
            if (msg == "release")
            {
                std::string objName(bufObj, objNameLen);
                std::string objPAddress(bufAddr, addrLen);
                RemoveProcess(objName, objPAddress);
                Enter(objectName, lockAcquired);
            }
            if (!lockAcquired)
            {
                Wait(objectName, lockAcquired);
            }
        }
    }
    void Pulse(string objectName)
    {
        if (waitingThreads.find(objectName) != waitingThreads.end())
        {
            for (map<string, State>::iterator it2 = waitingThreads.find(objectName)->second.begin(); it2 != waitingThreads.find(objectName)->second.end(); it2++)
            {
                if (it2->second == State::WAITING)
                {
                    string msg = "release";
                    string addr = it2->first;
                    int newPort = stoi(addr.substr(addr.size() - 4, 4));
                    newPort += 1;
                    addr.replace(addr.size() - 4, 4, to_string(newPort));
                    void* scktP = zmq_socket(context, ZMQ_REQ);
                    char* bufMsg = new char[256];
                    int timeout = 1000;
                    zmq_setsockopt(scktP, ZMQ_RCVTIMEO, &timeout, sizeof(int));
                    zmq_connect(scktP, addr.c_str());
                    if (zmq_send(scktP, msg.c_str(), strlen(msg.c_str()), 0) > -1)
                        if (zmq_recv(scktP, bufMsg, 256, 0) > -1)
                            if (zmq_send(scktP, ownAddress.c_str(), strlen(ownAddress.c_str()), 0) > -1)
                                if (zmq_recv(scktP, bufMsg, 256, 0) > -1)
                                    if (zmq_send(scktP, objectName.c_str(), strlen(objectName.c_str()), 0) > -1)
                                        zmq_recv(scktP, bufMsg, 256, 0);
                    zmq_close(scktP);
                    break;
                }
            }
        }
    }
    void PulseAll(string objectName)
    {
        if (waitingThreads.find(objectName) != waitingThreads.end())
        {
            SendAllMessage(objectName, "release", 1);
        }
    }
    void Exit(string objectName)
    {
        static map<string, map<string, State>>::iterator it = waitingThreads.find(objectName);
        if (it != waitingThreads.end() && it->second.find(ownAddress) != it->second.end())
        {
            PulseAll(objectName);
        }
    }
    void CheckForOtherProcesses()
    {
        char* bufMsg = new char[7];
        char* bufObj = new char[20];
        char* bufAddr = new char[256];
        int msgLen;
        int addrLen;
        int objNameLen;
        std::string msg;
        std::string objName;
        std::string addr;
        while (infinite)
        {
            CheckForMorePorts();
            if ((msgLen = zmq_recv(sckt, bufMsg, 7, 0)) > -1)
                if (zmq_send(sckt, "ok", 2, 0) > -1)
                    if ((addrLen = zmq_recv(sckt, bufAddr, 20, 0)) > -1)
                        if (zmq_send(sckt, "ok", 2, 0) > -1)
                            if ((objNameLen = zmq_recv(sckt, bufObj, 256, 0)) > -1)
                            {
                                zmq_send(sckt, "ok", 2, 0);
                                msg.assign(bufMsg, msgLen);
                                addr.assign(bufAddr, addrLen);
                                objName.assign(bufObj, objNameLen);
                                std::cout << "Got this message: " << endl << msg << endl << addr << endl << objName << endl;
                                if (msg == "waiting")
                                {
                                    std::cout << addr << " is waiting on " << objName << endl;
                                    AddProcess(objName, addr, State::WAITING);
                                }
                                else if (msg == "entered")
                                {
                                    std::cout << addr << " has enetred " << objName << endl;
                                    AddProcess(objName, addr, State::WORKING);
                                }
                                else if (msg == "release")
                                {
                                    std::cout << addr << " has released " << objName << endl;
                                    RemoveProcess(objName, addr);
                                }
                            }
        }
    }
    Monitor() {
        context = zmq_ctx_new();
        infinite = false;
    }
    Monitor(char* localPort)
    {
        ownPort = atoi(localPort);
        std::cout << "My port is " << ownPort << endl;
        context = zmq_ctx_new();
        sckt = zmq_socket(context, ZMQ_REP);
        std::ostringstream ss;
        ss << "tcp://127.0.0.1:" << localPort;
        ownAddress = ss.str();
        zmq_bind(sckt, ss.str().c_str());
        infinite = true;
        std::cout << "My ZMQ address is " << ownAddress << endl;
        checker = thread(&Monitor::CheckForOtherProcesses, this);
    }
    ~Monitor()
    {
        infinite = false;
        zmq_ctx_destroy(context);
        zmq_close(sckt);
    }
};
map<string, map<string, State>> Monitor::waitingThreads{ waitingThreads };
vector<string> Monitor::allProcessesAddresses;
int Monitor::ownPort;
mutex Monitor::mtx;
string Monitor::ownAddress;
void* Monitor::context;
void* Monitor::sckt;

int main(int argc, char* argv[])
{
    int mode = atoi(argv[2]);
    char* port = argv[1];
    //int mode = 2;
    //char* port = (char*)"2000";
    Monitor m(port);
    string objectName = "memoryAddressOrFileNameOrWhatever";
    bool lockAcquired = false;
    std::cout << "My mode is " << mode << endl;
    switch (mode)
    {
    case 1:
        Sleep(10000);
        std::cout << "1 entered sequence" << endl;
        m.Enter(objectName, lockAcquired);
        std::cout << "1 entered object" << endl;
        Sleep(10000);
        std::cout << "1 begins to wait object" << endl;
        m.Wait(objectName, lockAcquired);
        std::cout << "1 got object" << endl;
        Sleep(3000);
        m.Exit(objectName);
        std::cout << "1 exited object" << endl;
        break;
    case 2:
        Sleep(10000);
        std::cout << "2 entered sequence" << endl;
        m.Wait(objectName, 2000, lockAcquired);
        if (lockAcquired)
        {
            std::cout << "2 entered object" << endl;
        }
        else
        {
            std::cout << "2 waited 2 seconds on object and failed" << endl;
        }
        Sleep(10000);
        std::cout << "2 begins to wait object" << endl;
        m.Wait(objectName, lockAcquired);
        std::cout << "2 got object" << endl;
        m.Exit(objectName);
        std::cout << "2 exited object" << endl;
        break;
    case 3:
        Sleep(10000);
        std::cout << "3 entered sequence" << endl;
        m.Enter(objectName, lockAcquired);
        if (lockAcquired)
        {
            std::cout << "3 entered object" << endl;
        }
        else
        {
            std::cout << "3 begins to wait object" << endl;
            m.Wait(objectName, lockAcquired);
            std::cout << "3 got object" << endl;
        }
        m.Exit(objectName);
        std::cout << "3 exited object" << endl;
        break;
    default:
        break;
    }
    m.~Monitor();
    return 0;
}