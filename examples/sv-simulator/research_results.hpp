#pragma once
#include <QVariantList>
#include <boost/json.hpp>

// Bounded variant rows only; frame payloads/metadata never enter the QML model.
QVariantList researchResultRows(const boost::json::object &);
