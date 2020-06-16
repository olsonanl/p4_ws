
#include "AuthToken.h"
#include "SigningCerts.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include <ctime>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>

#include <boost/algorithm/string.hpp>

int main(int argc, char **argv)
{
    SSL_init();
    SigningCerts certs;
    
    AuthToken tok;

    std::cerr << sizeof(AuthToken) << "\n";
    std::ifstream tstr("/home/olson/.patric_token");
    tstr >> tok;
    std::cerr << tok << "\n";


    std::cerr << "valid: " << certs.validate(tok) << "\n";

    AuthToken t3(tok);
    std::cerr << t3 << "\n";

    AuthToken t2("hi there");
    std::cerr << t2 << "\n";

    SSL_finish();
    
    return 0;
}
