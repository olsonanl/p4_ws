#ifndef _Base64_h
#define _Base64_h


#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>

inline std::string base64EncodeText(std::string text) {
    const std::string base64_padding[] = {"", "==","="};

    using namespace boost::archive::iterators;
    typedef std::string::const_iterator iterator_type;
    typedef base64_from_binary<transform_width<iterator_type, 6, 8> > base64_enc;
    std::stringstream ss;
    std::copy(base64_enc(text.begin()), base64_enc(text.end()), std::ostream_iterator<char>(ss));
    ss << base64_padding[text.size() % 3];
    return ss.str();
}

#endif
