#include "ps5-model-io.hpp"
#include <cassert>
int main() {
    FILE *f = tmpfile(); assert(f);
    std::vector<unsigned char> data(19*1024*1024+123);
    for(size_t i=0;i<data.size();++i) data[i]=(i*17+i/1024)%251;
    assert(fwrite(data.data(),1,data.size(),f)==data.size());
    fflush(f);
    std::vector<unsigned char> output(17*1024*1024+11);
    assert(fseeko(f,73,SEEK_SET)==0);
    assert(prospero_ps5_read_parallel(f,output.data(),output.size()));
    assert(std::equal(output.begin(),output.end(),data.begin()+73));
    assert(ftello(f)==static_cast<off_t>(73+output.size()));
    assert(fseeko(f,data.size()-100,SEEK_SET)==0);
    auto before=ftello(f);
    assert(!prospero_ps5_read_parallel(f,output.data(),output.size()));
    assert(ftello(f)==before);
    assert(fseeko(f,31,SEEK_SET)==0);
    assert(!prospero_ps5_read_parallel(f,output.data(),100));
    assert(ftello(f)==31);
    fclose(f);
}
