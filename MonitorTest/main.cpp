#include <iostream>
#include <string>
#include "monitor.h"
using namespace std;
int main(int argc, char* argv[])
{
	int mode = atoi(argv[2]);
	char* port = argv[1];
	//int mode = 1;
	//char* port = (char*)"2002";
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
			cout << "2 entered on object" << endl;
		}
		else
		{
			cout << "2 waited 2 seconds on object and failed" << endl;
		}
		Sleep(10000);
		cout << "2 begins to wait on object" << endl;
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
			cout << "3 begins to wait on object" << endl;
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