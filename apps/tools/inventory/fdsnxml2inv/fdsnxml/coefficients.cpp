/***************************************************************************
 * Copyright (C) gempa GmbH                                                *
 * Contact: gempa GmbH (seiscomp-dev@gempa.de)                             *
 *                                                                         *
 * This file may be used under the terms of the GNU Affero                 *
 * Public License version 3.0 as published by the Free Software Foundation *
 * and appearing in the file LICENSE included in the packaging of this     *
 * file. Please review the following information to ensure the GNU Affero  *
 * Public License version 3.0 requirements will be met:                    *
 * https://www.gnu.org/licenses/agpl-3.0.html.                             *
 ***************************************************************************/


#define SEISCOMP_COMPONENT SWE
#include <fdsnxml/coefficients.h>
#include <fdsnxml/floatnounitwithnumbertype.h>
#include <fdsnxml/floatnounitwithnumbertype.h>
#include <algorithm>
#include <seiscomp/logging/log.h>


namespace Seiscomp {
namespace FDSNXML {


namespace {

static Seiscomp::Core::MetaEnumImpl<CfTransferFunctionType> metaCfTransferFunctionType;

}


Coefficients::MetaObject::MetaObject(const Core::RTTI *rtti, const Core::MetaObject *base) : Core::MetaObject(rtti, base) {
	addProperty(enumProperty("CfTransferFunctionType", "CfTransferFunctionType", false, false, &metaCfTransferFunctionType, &Coefficients::setCfTransferFunctionType, &Coefficients::cfTransferFunctionType));
	addProperty(arrayClassProperty<FloatNoUnitWithNumberType>("Numerator", "FDSNXML::FloatNoUnitWithNumberType", &Coefficients::numeratorCount, &Coefficients::numerator, static_cast<bool (Coefficients::*)(FloatNoUnitWithNumberType*)>(&Coefficients::addNumerator), &Coefficients::removeNumerator, static_cast<bool (Coefficients::*)(FloatNoUnitWithNumberType*)>(&Coefficients::removeNumerator)));
	addProperty(arrayClassProperty<FloatNoUnitWithNumberType>("Denominator", "FDSNXML::FloatNoUnitWithNumberType", &Coefficients::denominatorCount, &Coefficients::denominator, static_cast<bool (Coefficients::*)(FloatNoUnitWithNumberType*)>(&Coefficients::addDenominator), &Coefficients::removeDenominator, static_cast<bool (Coefficients::*)(FloatNoUnitWithNumberType*)>(&Coefficients::removeDenominator)));
}


IMPLEMENT_RTTI(Coefficients, "FDSNXML::Coefficients", BaseFilter)
IMPLEMENT_RTTI_METHODS(Coefficients)
IMPLEMENT_METAOBJECT_DERIVED(Coefficients, BaseFilter)


Coefficients::Coefficients() {
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Coefficients::Coefficients(const Coefficients &other)
 : BaseFilter() {
	*this = other;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Coefficients::~Coefficients() {}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Coefficients::operator==(const Coefficients &rhs) const {
	if ( !(_cfTransferFunctionType == rhs._cfTransferFunctionType) )
		return false;
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Coefficients::setCfTransferFunctionType(CfTransferFunctionType cfTransferFunctionType) {
	_cfTransferFunctionType = cfTransferFunctionType;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
CfTransferFunctionType Coefficients::cfTransferFunctionType() const {
	return _cfTransferFunctionType;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Coefficients& Coefficients::operator=(const Coefficients &other) {
	BaseFilter::operator=(other);
	_cfTransferFunctionType = other._cfTransferFunctionType;
	_numerators = other._numerators;
	_denominators = other._denominators;
	return *this;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
size_t Coefficients::numeratorCount() const {
	return _numerators.size();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
FloatNoUnitWithNumberType* Coefficients::numerator(size_t i) const {
	return _numerators[i].get();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Coefficients::addNumerator(FloatNoUnitWithNumberType *obj) {
	if ( obj == NULL )
		return false;

	// Add the element
	_numerators.push_back(obj);
	
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Coefficients::removeNumerator(FloatNoUnitWithNumberType *obj) {
	if ( obj == NULL )
		return false;

	std::vector<FloatNoUnitWithNumberTypePtr>::iterator it;
	it = std::find(_numerators.begin(), _numerators.end(), obj);
	// Element has not been found
	if ( it == _numerators.end() ) {
		SEISCOMP_ERROR("Coefficients::removeNumerator(FloatNoUnitWithNumberType*) -> child object has not been found although the parent pointer matches???");
		return false;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Coefficients::removeNumerator(size_t i) {
	// index out of bounds
	if ( i >= _numerators.size() )
		return false;

	_numerators.erase(_numerators.begin() + i);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
size_t Coefficients::denominatorCount() const {
	return _denominators.size();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
FloatNoUnitWithNumberType* Coefficients::denominator(size_t i) const {
	return _denominators[i].get();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Coefficients::addDenominator(FloatNoUnitWithNumberType *obj) {
	if ( obj == NULL )
		return false;

	// Add the element
	_denominators.push_back(obj);
	
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Coefficients::removeDenominator(FloatNoUnitWithNumberType *obj) {
	if ( obj == NULL )
		return false;

	std::vector<FloatNoUnitWithNumberTypePtr>::iterator it;
	it = std::find(_denominators.begin(), _denominators.end(), obj);
	// Element has not been found
	if ( it == _denominators.end() ) {
		SEISCOMP_ERROR("Coefficients::removeDenominator(FloatNoUnitWithNumberType*) -> child object has not been found although the parent pointer matches???");
		return false;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Coefficients::removeDenominator(size_t i) {
	// index out of bounds
	if ( i >= _denominators.size() )
		return false;

	_denominators.erase(_denominators.begin() + i);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
}
