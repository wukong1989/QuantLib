/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2018 Peter Caspers

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include "cmsspread.hpp"
#include "utilities.hpp"

#include <ql/cashflows/capflooredcoupon.hpp>
#include <ql/cashflows/cmscoupon.hpp>
#include <ql/cashflows/iborcoupon.hpp>
#include <ql/cashflows/lineartsrpricer.hpp>
#include <ql/experimental/coupons/cmsspreadcoupon.hpp>
#include <ql/experimental/coupons/lognormalcmsspreadpricer.hpp>
#include <ql/indexes/ibor/euribor.hpp>
#include <ql/indexes/swap/euriborswap.hpp>
#include <ql/instruments/swap.hpp>
#include <ql/instruments/swaption.hpp>
#include <ql/instruments/vanillaswap.hpp>
#include <ql/math/array.hpp>
#include <ql/math/comparison.hpp>
#include <ql/math/distributions/normaldistribution.hpp>
#include <ql/math/matrixutilities/pseudosqrt.hpp>
#include <ql/math/randomnumbers/sobolrsg.hpp>
#include <ql/pricingengines/swap/discountingswapengine.hpp>
#include <ql/pricingengines/swaption/gaussian1dswaptionengine.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/optionlet/constantoptionletvol.hpp>
#include <ql/termstructures/volatility/swaption/swaptionconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/target.hpp>
#include <ql/time/daycounters/actual360.hpp>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/make_shared.hpp>

using namespace QuantLib;
using namespace boost::unit_test_framework;
using namespace boost::accumulators;

namespace {
struct TestData {
    TestData() {
        refDate = Date(23, February, 2018);
        Settings::instance().evaluationDate() = refDate;

        yts2 = Handle<YieldTermStructure>(
            boost::make_shared<FlatForward>(refDate, 0.02, Actual365Fixed()));

        swLn = Handle<SwaptionVolatilityStructure>(
            boost::make_shared<ConstantSwaptionVolatility>(
                refDate, TARGET(), Following, 0.20, Actual365Fixed(),
                ShiftedLognormal, 0.0));
        swSln = Handle<SwaptionVolatilityStructure>(
            boost::make_shared<ConstantSwaptionVolatility>(
                refDate, TARGET(), Following, 0.10, Actual365Fixed(),
                ShiftedLognormal, 0.01));
        swN = Handle<SwaptionVolatilityStructure>(
            boost::make_shared<ConstantSwaptionVolatility>(
                refDate, TARGET(), Following, 0.0075, Actual365Fixed(), Normal,
                0.01));

        reversion = Handle<Quote>(boost::make_shared<SimpleQuote>(0.01));
        cmsPricerLn =
            boost::make_shared<LinearTsrPricer>(swLn, reversion, yts2);
        cmsPricerSln =
            boost::make_shared<LinearTsrPricer>(swSln, reversion, yts2);
        cmsPricerN = boost::make_shared<LinearTsrPricer>(swN, reversion, yts2);

        correlation = Handle<Quote>(boost::make_shared<SimpleQuote>(0.6));
        cmsspPricerLn = boost::make_shared<LognormalCmsSpreadPricer>(
            cmsPricerLn, correlation, yts2, 32);
        cmsspPricerSln = boost::make_shared<LognormalCmsSpreadPricer>(
            cmsPricerSln, correlation, yts2, 32);
        cmsspPricerN = boost::make_shared<LognormalCmsSpreadPricer>(
            cmsPricerN, correlation, yts2, 32);
    }

    SavedSettings backup;
    Date refDate;
    Handle<YieldTermStructure> yts2;
    Handle<SwaptionVolatilityStructure> swLn, swSln, swN;
    Handle<Quote> reversion, correlation;
    boost::shared_ptr<CmsCouponPricer> cmsPricerLn, cmsPricerSln, cmsPricerN;
    boost::shared_ptr<CmsSpreadCouponPricer> cmsspPricerLn, cmsspPricerSln,
        cmsspPricerN;
};
} // namespace

void CmsSpreadTest::testFixings() {
    BOOST_TEST_MESSAGE("Testing fixings of cms spread indices...");

    TestData d;

    auto cms10y =
        boost::make_shared<EuriborSwapIsdaFixA>(10 * Years, d.yts2, d.yts2);
    auto cms2y =
        boost::make_shared<EuriborSwapIsdaFixA>(2 * Years, d.yts2, d.yts2);
    auto cms10y2y =
        boost::make_shared<SwapSpreadIndex>("cms10y2y", cms10y, cms2y);

    Settings::instance().enforcesTodaysHistoricFixings() = false;

    BOOST_CHECK_THROW(cms10y2y->fixing(d.refDate - 1), QuantLib::Error);
    BOOST_REQUIRE_NO_THROW(cms10y2y->fixing(d.refDate));
    BOOST_CHECK_EQUAL(cms10y2y->fixing(d.refDate),
                      cms10y->fixing(d.refDate) - cms2y->fixing(d.refDate));
    cms10y->addFixing(d.refDate, 0.05);
    BOOST_CHECK_EQUAL(cms10y2y->fixing(d.refDate),
                      cms10y->fixing(d.refDate) - cms2y->fixing(d.refDate));
    cms2y->addFixing(d.refDate, 0.04);
    BOOST_CHECK_EQUAL(cms10y2y->fixing(d.refDate),
                      cms10y->fixing(d.refDate) - cms2y->fixing(d.refDate));
    Date futureFixingDate = TARGET().adjust(d.refDate + 1 * Years);
    BOOST_CHECK_EQUAL(cms10y2y->fixing(futureFixingDate),
                      cms10y->fixing(futureFixingDate) -
                          cms2y->fixing(futureFixingDate));
    IndexManager::instance().clearHistories();

    Settings::instance().enforcesTodaysHistoricFixings() = true;
    BOOST_CHECK_THROW(cms10y2y->fixing(d.refDate), QuantLib::Error);
    cms10y->addFixing(d.refDate, 0.05);
    BOOST_CHECK_THROW(cms10y2y->fixing(d.refDate), QuantLib::Error);
    cms2y->addFixing(d.refDate, 0.04);
    BOOST_CHECK_EQUAL(cms10y2y->fixing(d.refDate),
                      cms10y->fixing(d.refDate) - cms2y->fixing(d.refDate));
    IndexManager::instance().clearHistories();
}

namespace {
Real mcReferenceValue(const boost::shared_ptr<CmsCoupon>& cpn1,
                      const boost::shared_ptr<CmsCoupon>& cpn2, const Real cap,
                      const Real floor,
                      const Handle<SwaptionVolatilityStructure>& vol,
                      const Real correlation) {
    Size samples = 1000000;
    accumulator_set<double, stats<tag::mean> > acc;
    Matrix Cov(2, 2);
    Cov(0, 0) = vol->blackVariance(cpn1->fixingDate(), cpn1->index()->tenor(),
                                   cpn1->indexFixing());
    Cov(1, 1) = vol->blackVariance(cpn2->fixingDate(), cpn2->index()->tenor(),
                                   cpn2->indexFixing());
    Cov(0, 1) = Cov(1, 0) = std::sqrt(Cov(0, 0) * Cov(1, 1)) * correlation;
    Matrix C = pseudoSqrt(Cov);

    Array atmRate(2), adjRate(2), avg(2), volShift(2);
    atmRate[0] = cpn1->indexFixing();
    atmRate[1] = cpn2->indexFixing();
    adjRate[0] = cpn1->adjustedFixing();
    adjRate[1] = cpn2->adjustedFixing();
    if (vol->volatilityType() == ShiftedLognormal) {
        volShift[0] = vol->shift(cpn1->fixingDate(), cpn1->index()->tenor());
        volShift[1] = vol->shift(cpn2->fixingDate(), cpn2->index()->tenor());
        avg[0] =
            std::log((adjRate[0] + volShift[0]) / (atmRate[0] + volShift[0])) -
            0.5 * Cov(0, 0);
        avg[1] =
            std::log((adjRate[1] + volShift[1]) / (atmRate[1] + volShift[1])) -
            0.5 * Cov(1, 1);
    } else {
        avg[0] = adjRate[0];
        avg[1] = adjRate[1];
    }

    InverseCumulativeNormal icn;
    SobolRsg sb_(2, 42);
    Array w(2), z(2);
    for (Size i = 0; i < samples; ++i) {
        auto seq = sb_.nextSequence().value;
        std::transform(seq.begin(), seq.end(), w.begin(), icn);
        z = C * w + avg;
        for (Size i = 0; i < 2; ++i) {
            if (vol->volatilityType() == ShiftedLognormal) {
                z[i] =
                    (atmRate[i] + volShift[i]) * std::exp(z[i]) - volShift[i];
            }
        }
        acc(std::min(std::max(z[0] - z[1], floor), cap));
    }
    return mean(acc);
} // mcReferenceValue
} // namespace

void CmsSpreadTest::testCouponPricing() {
    BOOST_TEST_MESSAGE("Testing pricing of cms spread coupons...");

    TestData d;
    Real tol = 1E-6; // abs tolerance coupon rate

    auto cms10y =
        boost::make_shared<EuriborSwapIsdaFixA>(10 * Years, d.yts2, d.yts2);
    auto cms2y =
        boost::make_shared<EuriborSwapIsdaFixA>(2 * Years, d.yts2, d.yts2);
    auto cms10y2y =
        boost::make_shared<SwapSpreadIndex>("cms10y2y", cms10y, cms2y);

    Date valueDate = cms10y2y->valueDate(d.refDate);
    Date payDate = valueDate + 1 * Years;
    auto cpn1a = boost::make_shared<CmsCoupon>(
        payDate, 10000.0, valueDate, payDate, cms10y->fixingDays(), cms10y, 1.0,
        0.0, Date(), Date(), Actual360(), false);
    auto cpn1b = boost::make_shared<CmsCoupon>(
        payDate, 10000.0, valueDate, payDate, cms2y->fixingDays(), cms2y, 1.0,
        0.0, Date(), Date(), Actual360(), false);
    auto cpn1 = boost::make_shared<CmsSpreadCoupon>(
        payDate, 10000.0, valueDate, payDate, cms10y2y->fixingDays(), cms10y2y,
        1.0, 0.0, Date(), Date(), Actual360(), false);
    BOOST_CHECK(cpn1->fixingDate() == d.refDate);
    cpn1a->setPricer(d.cmsPricerLn);
    cpn1b->setPricer(d.cmsPricerLn);
    cpn1->setPricer(d.cmsspPricerLn);
    BOOST_CHECK_EQUAL(cpn1->rate(), cpn1a->rate() - cpn1b->rate());
    cms10y->addFixing(d.refDate, 0.05);
    BOOST_CHECK_EQUAL(cpn1->rate(), cpn1a->rate() - cpn1b->rate());
    cms2y->addFixing(d.refDate, 0.03);
    BOOST_CHECK_EQUAL(cpn1->rate(), cpn1a->rate() - cpn1b->rate());
    IndexManager::instance().clearHistories();

    auto cpn2a = boost::make_shared<CmsCoupon>(
        Date(23, February, 2029), 10000.0, Date(23, February, 2028),
        Date(23, February, 2029), 2, cms10y, 1.0, 0.0, Date(), Date(),
        Actual360(), false);
    auto cpn2b = boost::make_shared<CmsCoupon>(
        Date(23, February, 2029), 10000.0, Date(23, February, 2028),
        Date(23, February, 2029), 2, cms2y, 1.0, 0.0, Date(), Date(),
        Actual360(), false);

    auto plainCpn = boost::make_shared<CappedFlooredCmsSpreadCoupon>(
        Date(23, February, 2029), 10000.0, Date(23, February, 2028),
        Date(23, February, 2029), 2, cms10y2y, 1.0, 0.0, Null<Rate>(),
        Null<Rate>(), Date(), Date(), Actual360(), false);
    auto cappedCpn = boost::make_shared<CappedFlooredCmsSpreadCoupon>(
        Date(23, February, 2029), 10000.0, Date(23, February, 2028),
        Date(23, February, 2029), 2, cms10y2y, 1.0, 0.0, 0.03, Null<Rate>(),
        Date(), Date(), Actual360(), false);
    auto flooredCpn = boost::make_shared<CappedFlooredCmsSpreadCoupon>(
        Date(23, February, 2029), 10000.0, Date(23, February, 2028),
        Date(23, February, 2029), 2, cms10y2y, 1.0, 0.0, Null<Rate>(), 0.01,
        Date(), Date(), Actual360(), false);
    auto collaredCpn = boost::make_shared<CappedFlooredCmsSpreadCoupon>(
        Date(23, February, 2029), 10000.0, Date(23, February, 2028),
        Date(23, February, 2029), 2, cms10y2y, 1.0, 0.0, 0.03, 0.01, Date(),
        Date(), Actual360(), false);

    cpn2a->setPricer(d.cmsPricerLn);
    cpn2b->setPricer(d.cmsPricerLn);
    plainCpn->setPricer(d.cmsspPricerLn);
    cappedCpn->setPricer(d.cmsspPricerLn);
    flooredCpn->setPricer(d.cmsspPricerLn);
    collaredCpn->setPricer(d.cmsspPricerLn);

    BOOST_CHECK_SMALL(
        std::abs(plainCpn->rate() - mcReferenceValue(cpn2a, cpn2b, QL_MAX_REAL,
                                                     -QL_MAX_REAL, d.swLn,
                                                     d.correlation->value())),
        tol);
    BOOST_CHECK_SMALL(
        std::abs(cappedCpn->rate() - mcReferenceValue(cpn2a, cpn2b, 0.03,
                                                      -QL_MAX_REAL, d.swLn,
                                                      d.correlation->value())),
        tol);
    BOOST_CHECK_SMALL(
        std::abs(flooredCpn->rate() -
                 mcReferenceValue(cpn2a, cpn2b, QL_MAX_REAL, 0.01, d.swLn,
                                  d.correlation->value())),

        tol);
    BOOST_CHECK_SMALL(
        std::abs(collaredCpn->rate() -
                 mcReferenceValue(cpn2a, cpn2b, 0.03, 0.01, d.swLn,
                                  d.correlation->value())),
        tol);

    cpn2a->setPricer(d.cmsPricerSln);
    cpn2b->setPricer(d.cmsPricerSln);
    plainCpn->setPricer(d.cmsspPricerSln);
    cappedCpn->setPricer(d.cmsspPricerSln);
    flooredCpn->setPricer(d.cmsspPricerSln);
    collaredCpn->setPricer(d.cmsspPricerSln);

    BOOST_CHECK_SMALL(
        std::abs(plainCpn->rate() - mcReferenceValue(cpn2a, cpn2b, QL_MAX_REAL,
                                                     -QL_MAX_REAL, d.swSln,
                                                     d.correlation->value())),
        tol);
    BOOST_CHECK_SMALL(
        std::abs(cappedCpn->rate() - mcReferenceValue(cpn2a, cpn2b, 0.03,
                                                      -QL_MAX_REAL, d.swSln,
                                                      d.correlation->value())),
        tol);
    BOOST_CHECK_SMALL(
        std::abs(flooredCpn->rate() -
                 mcReferenceValue(cpn2a, cpn2b, QL_MAX_REAL, 0.01, d.swSln,
                                  d.correlation->value())),

        tol);
    BOOST_CHECK_SMALL(
        std::abs(collaredCpn->rate() -
                 mcReferenceValue(cpn2a, cpn2b, 0.03, 0.01, d.swSln,
                                  d.correlation->value())),
        tol);

    cpn2a->setPricer(d.cmsPricerN);
    cpn2b->setPricer(d.cmsPricerN);
    plainCpn->setPricer(d.cmsspPricerN);
    cappedCpn->setPricer(d.cmsspPricerN);
    flooredCpn->setPricer(d.cmsspPricerN);
    collaredCpn->setPricer(d.cmsspPricerN);

    BOOST_CHECK_SMALL(
        std::abs(plainCpn->rate() - mcReferenceValue(cpn2a, cpn2b, QL_MAX_REAL,
                                                     -QL_MAX_REAL, d.swN,
                                                     d.correlation->value())),
        tol);
    BOOST_CHECK_SMALL(
        std::abs(cappedCpn->rate() - mcReferenceValue(cpn2a, cpn2b, 0.03,
                                                      -QL_MAX_REAL, d.swN,
                                                      d.correlation->value())),
        tol);
    BOOST_CHECK_SMALL(std::abs(flooredCpn->rate() -
                               mcReferenceValue(cpn2a, cpn2b, QL_MAX_REAL, 0.01,
                                                d.swN, d.correlation->value())),

                      tol);
    BOOST_CHECK_SMALL(std::abs(collaredCpn->rate() -
                               mcReferenceValue(cpn2a, cpn2b, 0.03, 0.01, d.swN,
                                                d.correlation->value())),
                      tol);
}

test_suite* CmsSpreadTest::suite() {
    test_suite* suite = BOOST_TEST_SUITE("CmsSpreadTest");
    suite->add(QUANTLIB_TEST_CASE(&CmsSpreadTest::testFixings));
    suite->add(QUANTLIB_TEST_CASE(&CmsSpreadTest::testCouponPricing));
    return suite;
}
