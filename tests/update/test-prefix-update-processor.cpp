/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014-2017,  The University of Memphis,
 *                           Regents of the University of California,
 *                           Arizona Board of Regents.
 *
 * This file is part of NLSR (Named-data Link State Routing).
 * See AUTHORS.md for complete list of NLSR authors and contributors.
 *
 * NLSR is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NLSR is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NLSR, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "update/prefix-update-processor.hpp"

#include "../control-commands.hpp"
#include "../test-common.hpp"
#include "nlsr.hpp"

#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/mgmt/nfd/control-parameters.hpp>
#include <ndn-cxx/mgmt/nfd/control-response.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/dummy-client-face.hpp>

#include <boost/filesystem.hpp>

using namespace ndn;

namespace nlsr {
namespace update {
namespace test {

class PrefixUpdateFixture : public nlsr::test::UnitTestTimeFixture
{
public:
  PrefixUpdateFixture()
    : face(g_ioService, keyChain, {true, true})
    , siteIdentity(ndn::Name("/ndn/edu/test-site").appendVersion())
    , opIdentity(ndn::Name(siteIdentity).append(ndn::Name("%C1.Operator")).appendVersion())
    , nlsr(g_ioService, g_scheduler, face, g_keyChain)
    , keyPrefix(("/ndn/broadcast"))
    , namePrefixList(nlsr.getNamePrefixList())
    , updatePrefixUpdateProcessor(nlsr.getPrefixUpdateProcessor())
    , SITE_CERT_PATH(boost::filesystem::current_path() / std::string("site.cert"))
  {
    createSiteCert();
    BOOST_REQUIRE(siteCert != nullptr);

    createOperatorCert();
    BOOST_REQUIRE(opCert != nullptr);

    const std::string CONFIG =
      "rule\n"
      "{\n"
      "  id \"NLSR ControlCommand Rule\"\n"
      "  for interest\n"
      "  filter\n"
      "  {\n"
      "    type name\n"
      "    regex ^<localhost><nlsr><prefix-update>[<advertise><withdraw>]<>$\n"
      "  }\n"
      "  checker\n"
      "  {\n"
      "    type customized\n"
      "    sig-type rsa-sha256\n"
      "    key-locator\n"
      "    {\n"
      "      type name\n"
      "      regex ^([^<KEY><%C1.Operator>]*)<%C1.Operator>[^<KEY>]*<KEY><ksk-.*><ID-CERT>$\n"
      "    }\n"
      "  }\n"
      "}\n"
      "rule\n"
      "{\n"
      "  id \"NLSR Hierarchy Rule\"\n"
      "  for data\n"
      "  filter\n"
      "  {\n"
      "    type name\n"
      "    regex ^[^<KEY>]*<KEY><ksk-.*><ID-CERT><>$\n"
      "  }\n"
      "  checker\n"
      "  {\n"
      "    type hierarchical\n"
      "    sig-type rsa-sha256\n"
      "  }\n"
      "}\n"
      "trust-anchor\n"
      "{\n"
      " type file\n"
      " file-name \"site.cert\"\n"
      "}\n";

    const boost::filesystem::path CONFIG_PATH =
      (boost::filesystem::current_path() / std::string("unit-test.conf"));

    updatePrefixUpdateProcessor.getValidator().load(CONFIG, CONFIG_PATH.native());

    // Insert certs after the validator is loaded since ValidatorConfig::load() clears
    // the certificate cache
    nlsr.addCertificateToCache(opCert);

    // Set the network so the LSA prefix is constructed
    nlsr.getConfParameter().setNetwork("/ndn");

    // Initialize NLSR so a sync socket is created
    nlsr.initialize();

    // Saving clock::now before any advanceClocks so that it will
    // be the same value as what ChronoSync uses in setting the sessionName
    sessionTime.appendNumber(ndn::time::toUnixTimestamp(ndn::time::system_clock::now()).count());

    this->advanceClocks(ndn::time::milliseconds(10));

    face.sentInterests.clear();
  }

  void
  createSiteCert()
  {
    // Site cert
    keyChain.createIdentity(siteIdentity);
    siteCertName = keyChain.getDefaultCertificateNameForIdentity(siteIdentity);
    siteCert = keyChain.getCertificate(siteCertName);

    ndn::io::save(*siteCert, SITE_CERT_PATH.string());
  }

  void
  createOperatorCert()
  {
    // Operator cert
    ndn::Name keyName = keyChain.generateRsaKeyPairAsDefault(opIdentity, true);

    opCert = std::make_shared<ndn::IdentityCertificate>();
    std::shared_ptr<ndn::PublicKey> pubKey = keyChain.getPublicKey(keyName);
    opCertName = keyName.getPrefix(-1);
    opCertName.append("KEY").append(keyName.get(-1)).append("ID-CERT").appendVersion();
    opCert->setName(opCertName);
    opCert->setNotBefore(time::system_clock::now() - time::days(1));
    opCert->setNotAfter(time::system_clock::now() + time::days(1));
    opCert->setPublicKeyInfo(*pubKey);
    opCert->addSubjectDescription(ndn::security::v1::CertificateSubjectDescription(ndn::oid::ATTRIBUTE_NAME,
                                                                                   keyName.toUri()));
    opCert->encode();

    keyChain.signByIdentity(*opCert, siteIdentity);

    keyChain.addCertificateAsIdentityDefault(*opCert);
  }

  void sendInterestForPublishedData() {
    // Need to send an interest now since ChronoSync
    // no longer does face->put(*data) in publishData.
    // Instead it does it in onInterest
    ndn::Name lsaInterestName("/localhop/ndn/NLSR/LSA");
    lsaInterestName.append(std::to_string(Lsa::Type::NAME));

    // The part after LSA is Chronosync getSession
    lsaInterestName.append(sessionTime);
    lsaInterestName.appendNumber(nlsr.getLsdb().getSequencingManager().getNameLsaSeq());

    std::shared_ptr<Interest> lsaInterest = std::make_shared<Interest>(lsaInterestName);

    face.receive(*lsaInterest);
    this->advanceClocks(ndn::time::milliseconds(10));
  }

  bool
  wasRoutingUpdatePublished()
  {
    sendInterestForPublishedData();

    const ndn::Name& lsaPrefix = nlsr.getConfParameter().getLsaPrefix();

    const auto& it = std::find_if(face.sentData.begin(), face.sentData.end(),
      [lsaPrefix] (const ndn::Data& data) {
        return lsaPrefix.isPrefixOf(data.getName());
      });

    return (it != face.sentData.end());
  }

  void
  checkResponseCode(const Name& commandPrefix, uint64_t expectedCode)
  {
    std::vector<Data>::iterator it = std::find_if(face.sentData.begin(),
                                                  face.sentData.end(),
                                                  [commandPrefix] (const Data& data) {
                                                    return commandPrefix.isPrefixOf(data.getName());
                                                  });
    BOOST_REQUIRE(it != face.sentData.end());

    ndn::nfd::ControlResponse response(it->getContent().blockFromValue());
    BOOST_CHECK_EQUAL(response.getCode(), expectedCode);
  }

  ~PrefixUpdateFixture()
  {
    keyChain.deleteIdentity(siteIdentity);
    keyChain.deleteIdentity(opIdentity);

    boost::filesystem::remove(SITE_CERT_PATH);
  }

public:
  ndn::util::DummyClientFace face;
  ndn::KeyChain keyChain;

  ndn::Name siteIdentity;
  ndn::Name siteCertName;
  std::shared_ptr<IdentityCertificate> siteCert;

  ndn::Name opIdentity;
  ndn::Name opCertName;
  std::shared_ptr<IdentityCertificate> opCert;

  Nlsr nlsr;
  ndn::Name keyPrefix;
  NamePrefixList& namePrefixList;
  PrefixUpdateProcessor& updatePrefixUpdateProcessor;

  const boost::filesystem::path SITE_CERT_PATH;
  ndn::Name sessionTime;
};

BOOST_FIXTURE_TEST_SUITE(TestPrefixUpdateProcessor, PrefixUpdateFixture)

BOOST_AUTO_TEST_CASE(Basic)
{
  uint64_t nameLsaSeqNoBeforeInterest = nlsr.getLsdb().getSequencingManager().getNameLsaSeq();
  // Advertise
  ndn::nfd::ControlParameters parameters;
  parameters.setName("/prefix/to/advertise/");
  ndn::Name advertiseCommand("/localhost/nlsr/prefix-update/advertise");
  advertiseCommand.append(parameters.wireEncode());

  std::shared_ptr<Interest> advertiseInterest = std::make_shared<Interest>(advertiseCommand);
  keyChain.signByIdentity(*advertiseInterest, opIdentity);

  face.receive(*advertiseInterest);
  this->advanceClocks(ndn::time::milliseconds(10));

  NamePrefixList& namePrefixList = nlsr.getNamePrefixList();

  BOOST_REQUIRE_EQUAL(namePrefixList.size(), 1);
  BOOST_CHECK_EQUAL(namePrefixList.getNames().front(), parameters.getName());

  BOOST_CHECK(wasRoutingUpdatePublished());
  BOOST_CHECK(nameLsaSeqNoBeforeInterest < nlsr.getLsdb().getSequencingManager().getNameLsaSeq());

  face.sentData.clear();
  nameLsaSeqNoBeforeInterest = nlsr.getLsdb().getSequencingManager().getNameLsaSeq();

  // Withdraw
  ndn::Name withdrawCommand("/localhost/nlsr/prefix-update/withdraw");
  withdrawCommand.append(parameters.wireEncode());

  std::shared_ptr<Interest> withdrawInterest = std::make_shared<Interest>(withdrawCommand);
  keyChain.signByIdentity(*withdrawInterest, opIdentity);

  face.receive(*withdrawInterest);
  this->advanceClocks(ndn::time::milliseconds(10));

  BOOST_CHECK_EQUAL(namePrefixList.size(), 0);

  BOOST_CHECK(wasRoutingUpdatePublished());
  BOOST_CHECK(nameLsaSeqNoBeforeInterest < nlsr.getLsdb().getSequencingManager().getNameLsaSeq());
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace test
} // namespace update
} // namespace nlsr
