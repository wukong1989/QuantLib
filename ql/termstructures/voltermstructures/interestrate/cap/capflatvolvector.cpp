/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2007 Ferdinando Ametrano
 Copyright (C) 2007 Katiuscia Manzoni
 Copyright (C) 2000, 2001, 2002, 2003 RiskMap srl
 Copyright (C) 2003, 2004, 2005 StatPro Italia srl

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

#include <ql/termstructures/voltermstructures/interestrate/cap/capflatvolvector.hpp>
#include <ql/math/interpolations/cubicspline.hpp>
#include <ql/quotes/simplequote.hpp>

namespace QuantLib {

    // floating reference date, floating market data
    CapVolatilityVector::CapVolatilityVector(
                        Natural settlementDays,
                        const Calendar& calendar,
                        const std::vector<Period>& optionTenors,
                        const std::vector<Handle<Quote> >& vols,
                        BusinessDayConvention bdc,
                        const DayCounter& dc)
    : CapFloorVolatilityStructure(settlementDays, calendar, bdc, dc),
      optionTenors_(optionTenors),
      optionTimes_(optionTenors.size()),
      volHandles_(vols),
      volatilities_(vols.size())
    {
        checkInputs(vols.size());
        registerWithMarketData();
        for (Size i=0; i<volatilities_.size(); ++i)
            volatilities_[i] = volHandles_[i]->value();
        interpolate();
    }

    // fixed reference date, floating market data
    CapVolatilityVector::CapVolatilityVector(
                            const Date& settlementDate,
                            const Calendar& calendar,
                            const std::vector<Period>& optionTenors,
                            const std::vector<Handle<Quote> >& volatilities,
                            BusinessDayConvention bdc,
                            const DayCounter& dayCounter)
    : CapFloorVolatilityStructure(settlementDate, calendar, bdc, dayCounter),
      optionTenors_(optionTenors),
      optionTimes_(optionTenors.size()),
      volHandles_(volatilities),
      volatilities_(volatilities.size())
    {
        checkInputs(volatilities.size());
        registerWithMarketData();
        for (Size i=0; i<volatilities_.size(); ++i)
            volatilities_[i] = volHandles_[i]->value();
        interpolate();
    }

    // fixed reference date, fixed market data
    CapVolatilityVector::CapVolatilityVector(
                                const Date& settlementDate,
                                const Calendar& calendar,
                                const std::vector<Period>& optionTenors,
                                const std::vector<Volatility>& volatilities,
                                BusinessDayConvention bdc,
                                const DayCounter& dayCounter)
    : CapFloorVolatilityStructure(settlementDate, calendar, bdc, dayCounter),
      optionTenors_(optionTenors),
      optionTimes_(optionTenors.size()),
      volHandles_(volatilities.size()),
      volatilities_(volatilities.size())
    {
        checkInputs(volatilities.size());
        // fill dummy handles to allow generic handle-based computations later
        for (Size i=0; i<volatilities.size(); ++i) {
            volHandles_[i] = Handle<Quote>(boost::shared_ptr<Quote>(new
                SimpleQuote(volatilities[i])));
        }
        registerWithMarketData();
        interpolate();
    }

    // floating reference date, fixed market data
    CapVolatilityVector::CapVolatilityVector(
                                Natural settlementDays,
                                const Calendar& calendar,
                                const std::vector<Period>& optionTenors,
                                const std::vector<Volatility>& volatilities,
                                BusinessDayConvention bdc,
                                const DayCounter& dayCounter)
    : CapFloorVolatilityStructure(settlementDays, calendar, bdc, dayCounter),
      optionTenors_(optionTenors),
      optionTimes_(optionTenors.size()),
      volHandles_(volatilities.size()),
      volatilities_(volatilities.size())
    {
        checkInputs(volatilities.size());
        // fill dummy handles to allow generic handle-based computations later
        for (Size i=0; i<volatilities.size(); i++) {
            volHandles_[i] = Handle<Quote>(boost::shared_ptr<Quote>(new
                SimpleQuote(volatilities[i])));
        }
        registerWithMarketData();
        interpolate();
    }

    void CapVolatilityVector::checkInputs(Size volRows) const {
        QL_REQUIRE(optionTenors_.size()==volRows,
            "mismatch between number of option tenors ("
            << optionTenors_.size() <<
            ") and number of cap volatilities (" << volRows << ")");
        }

    void CapVolatilityVector::registerWithMarketData()
    {
        for (Size i=0; i<volHandles_.size(); ++i)
            registerWith(volHandles_[i]);
    }

    void CapVolatilityVector::interpolate() const
    {
        interpolation_ = CubicSpline(optionTimes_.begin(),
                                     optionTimes_.end(),
                                     volatilities_.begin(),
                                     CubicSpline::SecondDerivative,
                                     0.0,
                                     CubicSpline::SecondDerivative,
                                     0.0,
                                     false);
        //interpolation_ = LinearInterpolation(optionTimes_.begin(),
        //                                     optionTimes_.end(),
        //                                     volatilities_.begin());
    }

    void CapVolatilityVector::performCalculations() const {

        for (Size i=0; i<optionTenors_.size(); ++i) {
            Date endDate = optionDateFromTenor(optionTenors_[i]);
            optionTimes_[i] = timeFromReference(endDate);
        }

        for (Size i=0; i<volatilities_.size(); ++i)
            volatilities_[i] = volHandles_[i]->value();

        interpolation_.update();
    }

}