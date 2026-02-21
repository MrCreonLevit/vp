#include <iostream>
#include <string>
#include <algorithm>

using namespace std;

string str("XYZZY");

int main ()
{
  cout << "before: |" << str << "|" << endl;
  replace (str.begin(), str.end(), 'Y', 'X');
  cout << " after: |" << str << "|" << endl;
  return 0;
}
