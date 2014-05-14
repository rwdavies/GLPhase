
#include "gtest/gtest.h"
#include "hmmLike.hpp"
#include "sampler.hpp"
#include "glPack.hpp"
#include <cuda_runtime.h>
#include <gsl/gsl_rng.h>

using namespace std;

namespace HMMLikeCUDATest {
extern bool UnpackGLs(char GLset, float *GLs);
extern void FillEmit(const vector<float> &GLs, vector<float> &emit);
extern cudaError_t CopyTranToHost(vector<float> &tran);
extern cudaError_t CopyMutMatToHost(vector<float> &mutMat);
extern float CallHMMLike(unsigned idx, const unsigned (*hapIdxs)[4],
                         const vector<char> &packedGLs, unsigned packedGLStride,
                         const vector<uint64_t> &h_hapPanel);
}

TEST(FindDevice, FoundDevice) { HMMLikeCUDA::CheckDevice(); }

TEST(CopyToTransitionMat, CopySuccess) {

  vector<float> tran(NUMSITES * 3);
  for (int i = 0; i < NUMSITES * 3; ++i)
    tran[i] = 0.25;
  ASSERT_NEAR(tran[513], 0.25, 0.001);

  HMMLikeCUDA::CopyTranToDevice(tran);

  vector<float> postDTran(NUMSITES * 3);
  ASSERT_EQ(HMMLikeCUDATest::CopyTranToHost(postDTran),
            0); // 0 equals cudaSuccess
  ASSERT_EQ(postDTran.size(), NUMSITES * 3);
  for (unsigned i = 0; i < postDTran.size(); ++i)
    EXPECT_FLOAT_EQ(0.25, postDTran[i]);
}

TEST(CopyToMutMat, CopySuccess) {

  float pc[4][4];
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      pc[i][j] = i + 4 * j;

  ASSERT_FLOAT_EQ(pc[2][3], 2 + 4 * 3);
  HMMLikeCUDA::CopyMutationMatToDevice(&pc);

  vector<float> postDMutMat(4 * 4);
  ASSERT_EQ(HMMLikeCUDATest::CopyMutMatToHost(postDMutMat),
            0); // 0 equals cudaSuccess
  ASSERT_EQ(4 * 4, postDMutMat.size());
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      EXPECT_FLOAT_EQ(i + 4 * j, postDMutMat[i + 4 * j]);
}

// testing UnpackGLs
TEST(UnpackGLs, UnpackOk) {

  // testing silly vaules
  char num = 255;
  float GLs[3];
  for (int i = 0; i < 4; ++i)
    GLs[i] = 0;
  ASSERT_TRUE(HMMLikeCUDATest::UnpackGLs(num, GLs));
  EXPECT_FLOAT_EQ(15.5f / 16, GLs[0]);
  EXPECT_FLOAT_EQ(15.5f / 16, GLs[1]);
  EXPECT_FLOAT_EQ(0, GLs[2]);

  // testing realistic values
  num = 17;
  for (int i = 0; i < 4; ++i)
    GLs[i] = 0;
  ASSERT_TRUE(HMMLikeCUDATest::UnpackGLs(num, GLs));
  EXPECT_FLOAT_EQ(1.5f / 16, GLs[0]);
  EXPECT_FLOAT_EQ(1.5f / 16, GLs[1]);
  EXPECT_FLOAT_EQ(13.0f / 16, GLs[2]);
}

TEST(HMMLike, FillEmitFillsOK) {

  gsl_rng *rng = gsl_rng_alloc(gsl_rng_default);
  gsl_rng_set(rng, time(NULL));

  float mutMat[4][4];
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      mutMat[i][j] = 1.0f;
  HMMLikeCUDA::CopyMutationMatToDevice(&mutMat);

  vector<float> GLs(3);
  for (auto &GL : GLs)
    GL = gsl_rng_uniform(rng);
  vector<float> emit(4);
  HMMLikeCUDATest::FillEmit(GLs, emit);
  for (int i = 0; i < 4; ++i)
    EXPECT_FLOAT_EQ((GLs[0] + 2 * GLs[1] + GLs[2]), emit[i]);
}

TEST(HMMLike, createsOK) {
  gsl_rng *rng = gsl_rng_alloc(gsl_rng_default);
  gsl_rng_set(rng, 112);

  const unsigned numSamps = 2;
  const unsigned numHaps = 4;
  const unsigned numSites = 512;
  const unsigned wordSize = 64;
  const unsigned numWords = numSites / wordSize;
  vector<uint64_t> hapPanel(numWords * numHaps);
  vector<float> GLs(3 * numSites * numSamps);
  const unsigned sampleStride = 2;
  const unsigned numCycles = 100;

  // initialize transition matrix
  vector<float> tran(numSites * 3);
  float rho = 2 * 10e-8;
  for (unsigned m = numSites - 1; m; m--) {
    float rhoTDist = rho * 100; // approximate distance between SNPs
    float r = rhoTDist / (rhoTDist + numHaps);
    tran[m * 3] = (1 - r) * (1 - r);
    tran[m * 3 + 1] = r * (1 - r);
    tran[m * 3 + 2] = r * r; // for each position, transition.  r= alternative,
                             // 1-r= refrence? 4 state HMM with three
                             // transitions at each position
  }
  /* debugging
  cout << "printing tran sums: ";
  for (unsigned m = 0; m < tran.size(); m += 3)
    cout << tran[m] + 2 * tran[m + 1] + tran[m + 2] << " ";
  cout << endl;
  */

  // initialize mutation matrix
  float mutMat[4][4];
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      mutMat[i][j] = gsl_rng_uniform(rng);

  for (auto &GL : GLs)
    GL = gsl_rng_uniform(rng);
  GLPack glPack1(GLs, numSamps, sampleStride);

  UnifSampler sampler(rng, numSamps, numHaps);

  for (unsigned i = 0; i < numHaps * 10; ++i) {
    unsigned val = sampler.SampleHap(0);
    ASSERT_LT(1, val);
    ASSERT_GT(numHaps, val);
  }

  // now test HMMLike functionality
  HMMLike hmmLike(hapPanel, numHaps, glPack1, numCycles, tran, &mutMat, sampler,
                  *rng);

  unsigned firstSampIdx;
  unsigned lastSampIdx;
  {
    vector<unsigned> hapIdxs =
        hmmLike.RunHMMOnSamples(firstSampIdx, lastSampIdx);
    ASSERT_EQ(0, firstSampIdx);
    ASSERT_EQ(1, lastSampIdx);
    ASSERT_EQ(numSamps * 4, hapIdxs.size());
    for (int i = 0; i < 8; i += 2) {
      EXPECT_GT(4, hapIdxs[i]);
      EXPECT_LT(1, hapIdxs[i]);
    }
    for (int i = 1; i < 8; i += 2)
      EXPECT_GT(2, hapIdxs[i]);
  }

  {
    // test hmmLike function
    GLPack glPack0(GLs, numSamps, sampleStride);
    auto packedGLs = glPack0.GetPackedGLs();
    unsigned sampIdx = 0;
    unsigned fixedHapIdxs[4];
    for (int i = 0; i < 4; ++i)
      fixedHapIdxs[i] = 2;
    float like = HMMLikeCUDATest::CallHMMLike(
        sampIdx, &fixedHapIdxs, packedGLs, glPack0.GetSampleStride(), hapPanel);
    ASSERT_GE(1, like);
  }
  //  cout << "Likelihood of Model: " << like << endl << endl;

  // ok, let's try this again with a larger data set

  // this should be 40 haplotypes
  const unsigned bigNumHaps = 12;
  hapPanel.resize(numWords * bigNumHaps);
  hapPanel.at(5 *numWords) = ~0;
  hapPanel.at(6 *numWords) = ~0;
  hapPanel.at(5 *numWords + 5) = ~0;
  hapPanel.at(6 *numWords + 5) = ~0;

  hapPanel.at(7 *numWords + 1) = ~0;
  hapPanel.at(8 *numWords + 1) = ~0;
  hapPanel.at(7 *numWords + 7) = ~0;
  hapPanel.at(8 *numWords + 7) = ~0;

  // initialize site mutation probability matrix
  // diagonal is chance of no mutation
  // the diagonal "rotated by 90 degrees" is the chance of both positions
  // mutating
  // all other entries are chance of just one mutation
  const unsigned avgSiteDist = 1000;
  float mu = 0;
  for (unsigned i = 1; i < bigNumHaps; i++)
    mu += 1.0 / i;
  mu = 1 / mu;
  rho = 0.5 * mu * (numSites - 1) / (numSites * avgSiteDist) / 1;
  mu =
      mu / (bigNumHaps + mu); // rho is recombination rate?  mu is mutation rate

  mutMat[0][0] = mutMat[1][1] = mutMat[2][2] = mutMat[3][3] =
      (1 - mu) * (1 - mu); //	  probability of mutating no positions for each
  // parents haplotype
  mutMat[0][1] = mutMat[0][2] = mutMat[1][0] = mutMat[1][3] = mutMat[2][0] =
      mutMat[2][3] = mutMat[3][1] = mutMat[3][2] =
          mu * (1 - mu); //	  probability of mutating one position
  // for each parental haplotype
  mutMat[0][3] = mutMat[1][2] = mutMat[2][1] = mutMat[3][0] =
      mu * mu; //	  probability of mutating both positions for each
               // parental haplotype

  // now try creating tran based on mu
  for (unsigned m = numSites - 1; m; m--) {
    float rhoTDist = rho * avgSiteDist; // approximate distance between SNPs
    float r = rhoTDist / (rhoTDist + numHaps);
    tran[m * 3] = (1 - r) * (1 - r);
    tran[m * 3 + 1] = r * (1 - r);
    tran[m * 3 + 2] = r * r; // for each position, transition.  r= alternative,
                             // 1-r= refrence? 4 state HMM with three
                             // transitions at each position
  }

  // reset all GLs as HOM REF
  /*
    for (int i = 0; i < GLs.size(); i += 3) {
      GLs[i] = 1;
      GLs[i + 1] = 0;
      GLs[i + 2] = 0;
    }
  */
  {
    const float highExp = 100;
    unsigned offset = wordSize * 5 * 3;
    for (unsigned i = 0; i < wordSize * 3; i += 3) {
      // sample 0, 1st word set to ALT/ALT
      GLs.at(i + 2) = highExp;
      // sample 0, 6th word set to ALT/ALT
      GLs.at(offset + i + 2) = highExp;
      // sample 1, 2nd word set to ALT/ALT
      GLs.at(numSites * 3 + wordSize * 3 + i + 2) = highExp;
      // sample 1, 8th word set to ALT/ALT
      GLs.at(offset + i + 2 + numSites * 3 + wordSize * 2 * 3) = highExp;
    }
    assert(GLs.size() == numSites * numSamps * 3);
  }

  UnifSampler sampler2(rng, bigNumHaps / 2, bigNumHaps);

  // make sure sampler is giving us what we expect
  vector<unsigned> sampledHaps;
  for (unsigned i = 0; i < bigNumHaps * 10; ++i)
    sampledHaps.push_back(sampler2.SampleHap(0));
  ASSERT_EQ(bigNumHaps - 1,
            *std::max_element(sampledHaps.begin(), sampledHaps.end()));

  GLPack glPack2(GLs, numSamps, sampleStride);
  HMMLike hmmLike2(hapPanel, bigNumHaps, glPack2, numCycles, tran, &mutMat,
                   sampler2, *rng);

  firstSampIdx = 0;
  lastSampIdx = 0;
  {
    vector<unsigned> hapIdxs2 =
        hmmLike2.RunHMMOnSamples(firstSampIdx, lastSampIdx);
    ASSERT_EQ(0, firstSampIdx);
    ASSERT_EQ(1, lastSampIdx);
    ASSERT_EQ(numSamps * 4, hapIdxs2.size());

    // only one of the father and mother pairs needs to be correct
    for (unsigned i = 0; i < numSamps * 4; i += 2 * numSamps) {
      EXPECT_TRUE((7 > hapIdxs2[i] && 4 < hapIdxs2[i]) ||
                  (7 > hapIdxs2[i + numSamps] && 4 < hapIdxs2[i + numSamps]));
    }
    for (unsigned i = 1; i < numSamps * 4; i += 2 * numSamps) {
      EXPECT_TRUE((9 > hapIdxs2[i] && 6 < hapIdxs2[i]) ||
                  (9 > hapIdxs2[i + numSamps] && 6 < hapIdxs2[i + numSamps]));
    }
  }

  /*
    Now let's create a data set where both father and mother pairs are defined
  */
  // bigNumHaps should be 12
  hapPanel.at(10 *numWords + 2) = ~0;
  hapPanel.at(11 *numWords + 3) = ~0;

  /* debugging
  {
    vector<unsigned> haps = { 5, 6, 7, 8, 10, 11 };
    for (auto hapNum : haps) {
      cout << endl << "HapNum: " << hapNum << endl;
      for (unsigned wordNum = 0; wordNum < numWords; ++wordNum) {
        cout << hapPanel.at(numWords * hapNum + wordNum) << ' ';
      }
    }
    cout << endl;
  }
  */
  {
    const float highExp = 100;
    for (unsigned i = 0; i < wordSize * 3; i += 3) {
      // sample 0, 3rd word set to ALT/ALT
      GLs.at(i + 2 + wordSize * 3 * 2) = highExp;
      // sample 1, 4th word set to ALT/ALT
      GLs.at(i + 2 + wordSize * 3 * 3 + numSites * 3) = highExp;
    }
  }

  {
    const unsigned numCycles3 = 100;
    GLPack glPack3(GLs, numSamps, sampleStride);
    HMMLike hmmLike3(hapPanel, bigNumHaps, glPack3, numCycles3, tran, &mutMat,
                     sampler2, *rng);

    {
      // let's test the hmmLike function first
      GLPack glPack4(GLs, numSamps, sampleStride);
      auto packedGLs = glPack4.GetPackedGLs();
      unsigned sampIdx = 0;
      unsigned fixedHapIdxs[4];
      for (int i = 0; i < 4; ++i)
        fixedHapIdxs[i] = 2;
      float badLike =
          HMMLikeCUDATest::CallHMMLike(sampIdx, &fixedHapIdxs, packedGLs,
                                       glPack4.GetSampleStride(), hapPanel);
      ASSERT_GE(1, badLike);

      GLPack glPack5(GLs, numSamps, sampleStride);
      auto packedGLs2 = glPack4.GetPackedGLs();
      unsigned fixedHapIdxs2[4] = { 5, 10, 6, 10 };
      float goodLike =
          HMMLikeCUDATest::CallHMMLike(sampIdx, &fixedHapIdxs2, packedGLs2,
                                       glPack5.GetSampleStride(), hapPanel);

      ASSERT_GT(goodLike, badLike);
    }
    firstSampIdx = 0;
    lastSampIdx = 0;

    vector<unsigned> hapIdxs3 =
        hmmLike3.RunHMMOnSamples(firstSampIdx, lastSampIdx);
    ASSERT_EQ(0, firstSampIdx);
    ASSERT_EQ(1, lastSampIdx);
    ASSERT_EQ(numSamps * 4, hapIdxs3.size());

    // only one of the father and mother pairs needs to be correct
    for (unsigned i = 0; i < numSamps * 4; i += 2 * numSamps) {
      unsigned hap1 = hapIdxs3[i];
      unsigned hap2 = hapIdxs3[i + numSamps];
      if (hap1 > hap2)
        swap(hap1, hap2);
      EXPECT_EQ(10, hap2);
      EXPECT_LE(5, hap1);
      EXPECT_GE(6, hap1);

      // these are haps for second sample
      unsigned hap3 = hapIdxs3[i + 1];
      unsigned hap4 = hapIdxs3[i + 1 + numSamps];
      if (hap3 > hap4)
        swap(hap3, hap4);
      EXPECT_EQ(11, hap4);
      EXPECT_LE(7, hap3);
      EXPECT_GE(8, hap3);
    }
  }
  /*
    Now let's add two more samples to the GLs and try again
  */
  const unsigned numSamps2 = numSamps * 2;
  GLs.resize(3 * numSites * numSamps2);
  for (unsigned i = 3 * numSites * numSamps; i < 3 * numSites * numSamps2; ++i)
    GLs.at(i) = gsl_rng_uniform(rng);
  {
    const float highExp = 100;
    for (unsigned i = 0; i < wordSize * 3; i += 3) {
      // sample 2, 1st,4th and 6th word set to ALT/ALT
      GLs.at(i + 2 + 2 *numSites * 3) = highExp;
      GLs.at(i + 2 + 2 *numSites * 3 + wordSize * 3 * 3) = highExp;
      GLs.at(i + 2 + 2 *numSites * 3 + wordSize * 3 * 5) = highExp;

      // sample 3, 2nd,3rd and 8th word set to ALT/ALT
      GLs.at(i + 2 + 3 *numSites * 3 + wordSize * 3 * 1) = highExp;
      GLs.at(i + 2 + 3 *numSites * 3 + wordSize * 3 * 2) = highExp;
      GLs.at(i + 2 + 3 *numSites * 3 + wordSize * 3 * 7) = highExp;
    }
  }

  /* print GL state
     for debugging...
  {
    unsigned sampNum = 0;
    for (unsigned siteNum = 0; siteNum < numSites * numSamps2; ++siteNum) {
      if (siteNum % wordSize == 2)
        siteNum += wordSize - 3;

      if (siteNum % wordSize == 0)
        cout << endl;
      if (siteNum % numSites == 0) {
        cout << "SampNum: " << sampNum << endl;
        ++sampNum;
      }
      cout << GLs[siteNum * 3] << ',' << GLs[siteNum * 3 + 1] << ','
           << GLs[siteNum * 3 + 2] << endl;
    }
    cout << endl;
  }
  */
  
  {
    const unsigned numCycles3 = 100;
    ASSERT_EQ(3 * numSites * numSamps2, GLs.size());
    GLPack glPack3(GLs, numSamps2, sampleStride);
    HMMLike hmmLike3(hapPanel, bigNumHaps, glPack3, numCycles3, tran, &mutMat,
                     sampler2, *rng);

    firstSampIdx = 0;
    lastSampIdx = 0;
    {
      vector<unsigned> hapIdxs3 =
          hmmLike3.RunHMMOnSamples(firstSampIdx, lastSampIdx);
      ASSERT_EQ(0, firstSampIdx);
      ASSERT_EQ(1, lastSampIdx);
      ASSERT_EQ(numSamps * 4, hapIdxs3.size());

      // both of father and mother pairs need to be correct
      for (unsigned i = 0; i < numSamps * 4; i += 2 * numSamps) {
        unsigned hap1 = hapIdxs3[i];
        unsigned hap2 = hapIdxs3[i + numSamps];
        if (hap1 > hap2)
          swap(hap1, hap2);
        EXPECT_EQ(10, hap2);
        EXPECT_LE(5, hap1);
        EXPECT_GE(6, hap1);

        // these are haps for second sample
        unsigned hap3 = hapIdxs3[i + 1];
        unsigned hap4 = hapIdxs3[i + 1 + numSamps];
        if (hap3 > hap4)
          swap(hap3, hap4);
        EXPECT_EQ(11, hap4);
        EXPECT_LE(7, hap3);
        EXPECT_GE(8, hap3);
      }
    }
    {
      vector<unsigned> hapIdxs3 =
          hmmLike3.RunHMMOnSamples(firstSampIdx, lastSampIdx);
      ASSERT_EQ(2, firstSampIdx);
      ASSERT_EQ(3, lastSampIdx);
      ASSERT_EQ(numSamps * 4, hapIdxs3.size());

      // both of father and mother pairs need to be correct
      for (unsigned i = 0; i < numSamps * 4; i += 2 * numSamps) {
        unsigned hap1 = hapIdxs3[i];
        unsigned hap2 = hapIdxs3[i + numSamps];
        if (hap1 > hap2)
          swap(hap1, hap2);
        EXPECT_EQ(11, hap2);
        EXPECT_LE(5, hap1);
        EXPECT_GE(6, hap1);

        // these are haps for second sample
        unsigned hap3 = hapIdxs3[i + 1];
        unsigned hap4 = hapIdxs3[i + 1 + numSamps];
        if (hap3 > hap4)
          swap(hap3, hap4);
        EXPECT_EQ(10, hap4);
        EXPECT_LE(7, hap3);
        EXPECT_GE(8, hap3);
      }
    }
  }
}