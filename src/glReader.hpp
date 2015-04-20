/* @(#)glReader.hpp
 */

#ifndef _GLREADER_HPP
#define _GLREADER_HPP 1

#include <string>
#include <utility>
#include <vector>
#include "utils.hpp"
#include "bio.hpp"
#include "vcf.hpp"

namespace Bio {

namespace GLHelper {

enum class gl_t { BCF, STBIN };
enum class gl_ret_t { STANDARD, ST_DROP_FIRST };

struct init {
  std::string glFile;
  std::string nameFile;
  gl_t glType = gl_t::STBIN;
  gl_ret_t glRetType = gl_ret_t::ST_DROP_FIRST;
};
}

class GLReader {
private:
  GLHelper::init m_init;
  Bio::snp_storage_ordered m_sites;
  std::vector<std::string> m_names;
  std::vector<float> m_gls;

  void LoadNames();
  void LoadGLs();
  void LoadSTBinNames();
  void LoadBCFNames();
  void LoadSTBinGLs();
  void LoadBCFGLs();
  std::pair<std::vector<float>, Bio::snp_storage_ordered> GetSTBinGLs();

public:
  GLReader(){};
  GLReader(GLHelper::init init) : m_init(std::move(init)){};
  void clear() {
    m_sites.clear();
    m_names.clear();
    m_gls.clear();
  };

  // Setters
  void SetArgs(GLHelper::init init) {
    m_init = std::move(init);
    clear();
  }
  void SetRetGLType(GLHelper::gl_ret_t type) {
    m_init.glRetType = type;
    clear();
  }

  // Getters
  std::pair<std::vector<float>, Bio::snp_storage_ordered> GetGLs();
  std::vector<std::string> GetNames();
  std::string GetGLFile() const { return m_init.glFile; };
};
}
#endif /* _GLREADER_HPP */