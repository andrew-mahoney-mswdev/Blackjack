#include <iostream>
#include <string>
#include <fstream>
#include <stdio.h>
#include <io.h>
#include <time.h>

using namespace std;

string space = " ";

bool findfile (string path);

bool findfile (string path) {
  int found;
  _finddata_t filedata;
  bool exist = false;
  
  found = _findfirst(path.c_str(), &filedata);
  _findclose(found);

  if (found != -1) {exist = true;}  //Returns true if file exists. 
  return exist;
}

void remove(string filename) {remove(filename.c_str());}

enum filestateenum {in, out, app};

class filemngr {
protected:
  string path;
  bool isopen;
  bool isnew;
  filestateenum curfilestate;
  ofstream *outfile;
  ifstream *infile;
  void openin();
  void openout();
  void openapp();
  void closefile();
public:
  filemngr(string filename, filestateenum inoutapp);
  ~filemngr();
  void changestate(filestateenum inoutapp);
  bool geteof();
  bool getisnew();
  filestateenum getstate();
  void writeline(string towrite);
  void writeline(char* towrite); //Handles text passed solely within ""
  void writeline(int towrite);
  void writeline(unsigned int towrite);
  void writeline(float towrite);
  void writeline(bool towrite);
  void writeline(time_t towrite);
  void writeline();
  void writeword(string towrite);
  void writeword(char* towrite);
  void writeword(int towrite);
  void writeword(unsigned int towrite);
  void writeword(float towrite);
  void writeword(bool towrite);
  void writeword(time_t towrite);
  string readline();
  string readstring();
  int readint();
  unsigned int readunsignedint();
  float readfloat();
  bool readbool();
  time_t readtime_t();
  string readword(string *toread);
  int readword(int *toread);
  unsigned int readword(unsigned int *toread);
  float readword(float *toread);
  bool readword(bool *toread);
  time_t readword(time_t *toread);
};

filemngr::filemngr(string filename, filestateenum filestate) {
  path = filename;
  isopen = false;
  if (findfile(path) == false) {isnew = true;}
  else {isnew = false;}
  changestate(filestate);
}

filemngr::~filemngr() {
  closefile();
}

void filemngr::openin() {
  if (isopen == true) {closefile();}
  infile = new ifstream(path.c_str(), ios::in);
  isopen = true;
}

void filemngr::openout() {
  if (isopen == true) {closefile();}
  outfile = new ofstream(path.c_str(), ios::trunc);
  isopen = true;
}

void filemngr::openapp() {
  if (isopen == true) {closefile();}
  outfile = new ofstream(path.c_str(), ios::app);
  isopen = true;
}

void filemngr::closefile() {
  if (curfilestate != in) {
    (*outfile).close();
    isopen = false;
    delete outfile;
  }
  if (curfilestate == in) {
    (*infile).close();
    isopen = false;
    delete infile;
  }
}

void filemngr::changestate(filestateenum filestate) {
  if (filestate == in) {openin();} else
  if (filestate == out) {openout();} else
  if (filestate == app) {openapp();}
  curfilestate = filestate;
}

bool filemngr::geteof(){
    if (curfilestate == in) {return (*infile).eof();}
}

bool filemngr::getisnew() {
  return isnew;
}

filestateenum filemngr::getstate(){
    return curfilestate;
}

void filemngr::writeline(string data) {
  if (curfilestate != in) {*outfile << data << endl;}
}

void filemngr::writeline(char* data) {
  if (curfilestate != in) {*outfile << data << endl;}
}

void filemngr::writeline(int data) {
  if (curfilestate != in) {*outfile << data << endl;}
}

void filemngr::writeline(unsigned int data) {
  if (curfilestate != in) {*outfile << data << endl;}
}

void filemngr::writeline(float data) {
  if (curfilestate != in) {*outfile << data << endl;}
}

void filemngr::writeline(bool data) {
  if (curfilestate != in) {*outfile << data << endl;}
}

void filemngr::writeline(time_t data) {
  if (curfilestate != in) {*outfile << data << endl;}
}

void filemngr::writeline() {
  if (curfilestate != in) {*outfile << endl;}
}

void filemngr::writeword(string data) {
  if (curfilestate != in) {*outfile << data << space;}
}

void filemngr::writeword(char* data) {
  if (curfilestate != in) {*outfile << data << space;}
}

void filemngr::writeword(int data) {
  if (curfilestate != in) {*outfile << data << space;}
}

void filemngr::writeword(unsigned int data) {
  if (curfilestate != in) {*outfile << data << space;}
}

void filemngr::writeword(float data) {
  if (curfilestate != in) {*outfile << data << space;}
}

void filemngr::writeword(bool data) {
  if (curfilestate != in) {*outfile << data << space;}
}

void filemngr::writeword(time_t data) {
  if (curfilestate != in) {*outfile << data << space;}
}

string filemngr::readstring() {
  string data;
  if (curfilestate == in && (*infile).eof() == false) {*infile >> data;}
  return data;
}

string filemngr::readword(string *toread) {
  *toread = readstring();
  return *toread;
}

string filemngr::readline() {
  string data;
  if (curfilestate == in && (*infile).eof() == false) {getline(*infile, data);}
  return data;
}

int filemngr::readint() {
  int data;
  if (curfilestate == in && (*infile).eof() == false) {*infile >> data;}
  return data;
}

int filemngr::readword(int *toread) {
  *toread = readint();
  return *toread;
}

unsigned int filemngr::readunsignedint() {
  unsigned int data;
  if (curfilestate == in && (*infile).eof() == false) {*infile >> data;}
  return data;
}

unsigned int filemngr::readword(unsigned int *toread) {
  *toread = readunsignedint();
  return *toread;
}

float filemngr::readfloat() {
  float data;
  if (curfilestate == in && (*infile).eof() == false) {*infile >> data;}
  return data;
}

float filemngr::readword(float *toread) {
  *toread = readfloat();
  return *toread;
}

bool filemngr::readbool() {
  bool data;
  if (curfilestate == in && (*infile).eof() == false) {*infile >> data;}
  return data;
}

bool filemngr::readword(bool *toread) {
  *toread = readbool();
  return *toread;
}

time_t filemngr::readtime_t() {
  time_t data;
  if (curfilestate == in && (*infile).eof() == false) {*infile >> data;}
  return data;
}

time_t filemngr::readword(time_t *toread) {
  *toread = readtime_t();
  return *toread;
}
