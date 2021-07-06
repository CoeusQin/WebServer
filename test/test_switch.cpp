#include <iostream>
using namespace std;


int main()
{
    int n = 5;
    int state = 1;
    while (n--)
    {
        switch (state)
        {
        case 1:
        {
            cout << "state:1" << endl;
            state++;
            break;
        }
        
        case 2:
        {
            cout << "state:2" << endl;
            state++;
            break;
        }

        case 3:
        {
            cout << "state:3" << endl;
            state++;
            break;
        }
        
        default:
        {
            cout << "state>3: " << state << endl;
            break;
        }  
        }
    }
}