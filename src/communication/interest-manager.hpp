#ifndef NLSR_IM_HPP
#define NLSR_IM_HPP

#include <ndn-cpp-dev/face.hpp>
#include <ndn-cpp-dev/security/key-chain.hpp>
#include <ndn-cpp-dev/util/scheduler.hpp>

namespace nlsr {

class Nlsr;

class InterestManager
{
public:
  InterestManager()
  {
  }
  void
  processInterest(Nlsr& pnlsr, const ndn::Name& name,
                  const ndn::Interest& interest);

  void
  processInterestInfo(Nlsr& pnlsr, std::string& neighbor,
                      const ndn::Interest& interest);

  void
  processInterestLsa(Nlsr& pnlsr, const ndn::Interest& interest);

  void
  processInterestForNameLsa(Nlsr& pnlsr, const ndn::Interest& interest,
                            std::string lsaKey, uint32_t interestedlsSeqNo);

  void
  processInterestForAdjLsa(Nlsr& pnlsr, const ndn::Interest& interest,
                           std::string lsaKey, uint32_t interestedlsSeqNo);

  void
  processInterestForCorLsa(Nlsr& pnlsr, const ndn::Interest& interest,
                           std::string lsaKey, uint32_t interestedlsSeqNo);

  void
  processInterestKeys(Nlsr& pnlsr, const ndn::Interest& interest);

  void
  processInterestTimedOut(Nlsr& pnlsr, const ndn::Interest& interest);

  void
  processInterestTimedOutInfo(Nlsr& pnlsr, std::string& neighbor,
                              const ndn::Interest& interest);

  void
  processInterestTimedOutLsa(Nlsr& pnlsr, const ndn::Interest& interest);

  void
  expressInterest(Nlsr& pnlsr,
                  const std::string& interestNamePrefix, int scope, int seconds);

  void
  sendScheduledInfoInterest(Nlsr& pnlsr, int seconds);

  void
  scheduleInfoInterest(Nlsr& pnlsr, int seconds);

private:


};

}//namespace nlsr

#endif //NLSR_IM_HPP
