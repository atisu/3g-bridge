#ifndef __CG_ALG_H_
#define __CG_ALG_H_

#include <string>

using namespace std;

enum CGAlgType {
    CG_ALG_DB,
    CG_ALG_LOCAL,
    CG_ALG_GRID
};

class CGAlg {
public:
    CGAlg(const string tname, const enum CGAlgType ttype):name(tname),type(ttype) {}
    string getName() { return name; }
    enum CGAlgType getType() { return type; }
private:
    string name;
    enum CGAlgType type;
};

#endif
