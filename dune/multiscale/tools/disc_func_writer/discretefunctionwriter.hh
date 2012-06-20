/**
   *  \file discretefunctionwriter.hh
   *  \brief  write a bunch of discrete functions to one file and retrieve 'em
   **/

#ifndef DISCRETEFUNCTIONWRITER_HEADERGUARD
#define DISCRETEFUNCTIONWRITER_HEADERGUARD

#include <fstream>
#include <vector>
#include <cassert>

#include <dune/fem/misc/mpimanager.hh> // An initializer of MPI

class DiscreteFunctionWriter
{
public:
  DiscreteFunctionWriter(const std::string filename)
    : filename_(filename)
  {}

  ~DiscreteFunctionWriter() {
    if ( file_.is_open() )
      file_.close();
  }

  bool open() {
    typedef std::fstream
    fs;
    fs::openmode mode = (fs::trunc | fs::out | fs::binary);
    file_.open(filename_.c_str(), mode);
    return file_.is_open();
  } // open

  template< class DFType >
  void append(const DFType& df) {
    assert( file_.is_open() );
    typedef typename DFType::DomainFieldType
    Field;
    typedef typename DFType::ConstDofIteratorType
    Iter;
    Iter end = df.dend();
    for (Iter it = df.dbegin(); it != end; ++it)
    {
      double d = *it;
      file_.write( reinterpret_cast< char* >(&d), sizeof(Field) );
    }
  } // append

  template< class DFType >
  void append(const std::vector< DFType >& df_vec) {
    typedef typename std::vector< DFType >::const_iterator
    Iter;
    Iter end = df_vec.end();
    for (Iter it = df_vec.begin(); it != end; ++it)
    {
      append(*it);
    }
  } // append

private:
  const std::string filename_;
  std::fstream file_;
};

class DiscreteFunctionReader
{
public:
  DiscreteFunctionReader(const std::string filename)
    : filename_(filename)
      , size_(-1)
  {}

  ~DiscreteFunctionReader() {
    if ( file_.is_open() )
      file_.close();
  }

  bool open() {
    typedef std::fstream
    fs;
    fs::openmode mode = (fs::in | fs::binary);
    file_.open(filename_.c_str(), mode);
    bool ok = file_.is_open();
    if (ok)
    {
      // get size of file
      file_.seekg(0, fs::end);
      size_ = file_.tellg();
      file_.seekg(0);
    }
    return ok;
  } // open

  long size() {
    return size_;
  }

  void close() {
    file_.close();
  }

  template< class DFType >
  void read(const unsigned long index, DFType& df) {
    typedef typename DFType::DomainFieldType
    Field;
    unsigned long bytes = df.size() * sizeof(Field);

    assert( file_.is_open() );
    assert( size_ >= long( bytes * (index + 1) ) );
    file_.seekg(bytes * index);

    typedef typename DFType::DofIteratorType
    Iter;
    Iter end = df.dend();
    for (Iter it = df.dbegin(); it != end; ++it)
    {
      file_.read( reinterpret_cast< char* >( &(*it) ), sizeof(Field) );
    }
  } // read

  /*template < class DFType >
     * void read( std::vector<DFType>& df_vec )
     * {
     *  for ( Iter it = df_vec.begin(); it != end; ++it ) {
     * append( *it );
     *  }
     * }*/

private:
  const std::string filename_;
  long size_;
  std::fstream file_;
};

#endif // ifndef DISCRETEFUNCTIONWRITER_HEADERGUARD
