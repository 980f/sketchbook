#pragma once

///////////////////////////////////////////////////////////////////////
//copied in from 980f's library
template <typename Scalar, typename ScalarArg> bool changed(Scalar &target, const ScalarArg &source) {
  if (target != source) { //implied conversion from ScalarArg to Scalar must exist or compiler will barf on this line.
    target = source;
    return true;
  }
  return false;
}

bool flagged(bool &flag) {
  auto was = flag;
  flag = false;
  return was;
}

