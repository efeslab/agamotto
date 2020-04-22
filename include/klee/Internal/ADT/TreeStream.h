//===-- TreeStream.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_TREESTREAM_H
#define KLEE_TREESTREAM_H

#include <string>
#include <vector>
#include <iostream>
#include <cassert>
#include <map>
#include <iomanip>
#include <fstream>

#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "TreeStreamWriter"
#include "klee/Internal/Support/Debug.h"
#include "klee/Internal/Support/Serialize.h"

namespace klee {
  typedef unsigned TreeStreamID;
  class TreeOStream;

  class TreeStreamWriter {

    friend class TreeOStream;

  private:
    unsigned lastID, lastLen;
    std::iostream::pos_type lastLen_off;
    bool isWritten, lastLen_dirty;

    std::string path;
    std::ofstream *output;
    unsigned ids;

    template<typename T>
    void write(TreeOStream &os, const T &entry);
    void write_metadata(TreeOStream &os);
    void flush_lastLen();

  public:
    TreeStreamWriter(const std::string &_path);
    ~TreeStreamWriter();

    bool good();

    TreeOStream open();
    TreeOStream open(const TreeOStream &node);

    void flush();

    // hack, to be replace by proper stream capabilities
    template <typename T>
    void readStream(TreeStreamID streamID, std::vector<T> &out);
  };

  class TreeOStream {
      friend class TreeStreamWriter;

      private:
      TreeStreamWriter *writer;
      unsigned id;

      TreeOStream(TreeStreamWriter &_writer, unsigned _id, unsigned int _cnt);

      public:
      unsigned int cnt = 0;
      TreeOStream();
      ~TreeOStream();

      unsigned getID() const;
      bool isValid() const { return writer != NULL; }

      template<typename T> TreeOStream &operator<<(const T &entry) {
          ++cnt;
          assert(writer);
          writer->write(*this, entry);
          return *this;
      }

      void flush();
      TreeOStream branch() {
        return writer->open(*this);
      }
  };
  template<typename T>
  void TreeStreamWriter::write(TreeOStream &os, const T &entry) {
    if (!isWritten || 
        (isWritten && os.id != lastID)) {
      write_metadata(os);
    }
    serialize(*output, entry);
    ++lastLen;
    lastLen_dirty = true;
  }
  template <typename T>
  void TreeStreamWriter::readStream(TreeStreamID streamID,
          std::vector<T> &out) {
      assert(streamID>0 && streamID<ids);
      flush();

      std::ifstream is(path.c_str(),
              std::ios::in | std::ios::binary);
      assert(is.good());
      KLEE_DEBUG(llvm::errs() << "finding chain for: " << streamID << "\n");

      std::map<unsigned,unsigned> parents;
      std::vector<unsigned> roots;
      for (;;) {
          assert(is.good());
          unsigned id;
          unsigned tag;
          is.read(reinterpret_cast<char*>(&id), sizeof(id));
          is.read(reinterpret_cast<char*>(&tag), sizeof(tag));
          if (tag&(1<<31)) { // fork
              unsigned child = tag ^ (1<<31);

              if (child==streamID) {
                  roots.push_back(child);
                  while (id) {
                      roots.push_back(id);
                      std::map<unsigned, unsigned>::iterator it = parents.find(id);
                      assert(it!=parents.end());
                      id = it->second;
                  }
                  break;
              } else {
                  parents.insert(std::make_pair(child,id));
              }
          } else {
              unsigned len = tag;
              for (unsigned i=0; i < len; ++i) {
                  skip(is, T());
              }
          }
      }
      KLEE_DEBUG({
              llvm::errs() << "roots: ";
              for (size_t i = 0, e = roots.size(); i < e; ++i) {
              llvm::errs() << roots[i] << " ";
              }
              llvm::errs() << "\n";
              });
      is.seekg(0, std::ios::beg);
      for (;;) {
          unsigned id;
          unsigned tag;
          is.read(reinterpret_cast<char*>(&id), 4);
          is.read(reinterpret_cast<char*>(&tag), 4);
          if (!is.good()) break;
          if (tag&(1<<31)) { // fork
              unsigned child = tag ^ (1<<31);
              if (id==roots.back() && roots.size()>1 && child==roots[roots.size()-2])
                  roots.pop_back();
          } else {
              unsigned len = tag;
              T entry;
              for (unsigned i=0; i < len; ++i) {
                  if (id==roots.back()) {
                      deserialize(is, entry);
                      out.push_back(std::move(entry));
                  } else {
                      skip(is, entry);
                  }
              }
          }
      }
  }
}

#endif /* KLEE_TREESTREAM_H */
