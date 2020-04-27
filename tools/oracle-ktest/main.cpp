#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/Support/PrintVersion.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Signals.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <sstream>
#include <unordered_map>

using namespace llvm;
using namespace klee;

static cl::opt<std::string> InputFile(cl::desc("input_template_file"), cl::Positional, cl::Required);
static cl::opt<std::string> OutputFile(cl::desc("output_file"), cl::Positional, cl::Required);
static cl::extrahelp inputFormatHelp(
        "Input template file format:\n"
        "  each line consists of an input format, then input file path, "
        "then the symbolic object name it provides\n"
        "  e.g.,\n"
        "    KTEST /tmp/1.ktest A-obj B-obj\n"
        "    KTEST a.ktest *\n"
        "    PLAIN 2.bin C-obj\n"
        );

KTest output_KTest;
std::unordered_map<std::string, KTest *> ktest_cache;
std::unordered_map<std::string, KTestObject> ktest_objs;

static KTestObject *GetObjFromKTestByName(KTest *ktest, std::string &name) {
    for (unsigned i = 0; i < ktest->numObjects; ++i) {
        if (name.compare(ktest->objects[i].name) == 0) {
            return &(ktest->objects[i]);
        }
    }
    return NULL;
}

static void AddObject(std::string &name, KTestObject obj) {
    std::pair<std::unordered_map<std::string, KTestObject>::iterator, bool> ret =
        ktest_objs.insert(std::make_pair(name, obj));
    if (!ret.second) {
        std::cout << "OverWritting " << name << std::endl;
    }
    auto iter = ret.first;
    iter->second = obj;
}

static KTest *GetKTest(std::string &path) {
    auto it = ktest_cache.find(path);
    KTest *ktest;
    if (it != ktest_cache.end()) {
        // found already opened KTest File
        ktest = it->second;
    }
    else {
        // open new KTest File
        ktest = kTest_fromFile(path.c_str());
        if (!ktest) {
            std::cout << "Invalid KTest file at " << path << std::endl;
            abort();
        }
        else {
            // add just opened KTest File to cache
            ktest_cache.insert(std::make_pair(path, ktest));
        }
    }
    return ktest;
}

static void HideOptions(cl::OptionCategory &Category) {
    StringMap<cl::Option *> &map = cl::getRegisteredOptions();
    for (auto &elem : map) {
        if (elem.second->Category == &Category) {
            elem.second->setHiddenFlag(cl::Hidden);
        }
    }
}

int main(int argc, char **argv) {
    sys::PrintStackTraceOnErrorSignal(argv[0]);
    HideOptions(llvm::cl::GeneralCategory);
    cl::SetVersionPrinter(klee::printVersion);
    cl::ParseCommandLineOptions(argc, argv);

    // init output_KTest
    std::memset(&output_KTest, 0, sizeof(output_KTest));
    output_KTest.version = kTest_getCurrentVersion();

    std::ifstream inFile(InputFile);
    assert(inFile.good() && "Cannot open input file");

    while (1) {
        std::istringstream line;
        std::string linestr, format, path, objname;
        std::getline(inFile, linestr);
        if (!inFile.good()) {
            break;
        }
        else {
            line.str(linestr);
            line >> format >> path;
        }
        if (format == "KTEST") {
            KTest *ktest = GetKTest(path);
            while (line.good()) {
                line >> objname;
                if (objname == "*") {
                    for (unsigned i = 0; i < ktest->numObjects; ++i) {
                        std::string name(ktest->objects[i].name);
                        AddObject(name, ktest->objects[i]);
                    }
                    // when having star, all objects from this KTest has already been included
                    break;
                }
                else {
                    KTestObject *kobj = GetObjFromKTestByName(ktest, objname);
                    AddObject(objname, *kobj);
                }
            }
        }
        else if (format == "PLAIN") {
            std::ifstream plainfile(path, std::ios::binary);
            assert(line.good() && "objname is required for PLAIN file");
            line >> objname;
            KTestObject kobj;
            if (!plainfile.good()) {
                std::cout << "Cannot open plain file " << path << std::endl;
                abort();
            }
            plainfile.seekg(0, std::ios::end);
            kobj.name = new char[objname.size() + 1];
            std::memcpy(kobj.name, objname.c_str(), objname.size() + 1);
            kobj.numBytes = plainfile.tellg();
            plainfile.seekg(0);
            kobj.bytes = new unsigned char[kobj.numBytes];
            plainfile.read(reinterpret_cast<char*>(kobj.bytes), kobj.numBytes);
            AddObject(objname, kobj);
        }
    }

    output_KTest.numObjects = ktest_objs.size();
    output_KTest.objects = new KTestObject[output_KTest.numObjects];
    KTestObject *kobj = output_KTest.objects;
    for (auto mit: ktest_objs) {
        *kobj = mit.second;
        ++kobj;
    }

    kTest_toFile(&output_KTest, OutputFile.c_str());

    delete [] output_KTest.objects;
    for (auto cit: ktest_cache) {
        kTest_free(cit.second);
    }
}
