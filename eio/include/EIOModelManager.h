/*  
   Elmer, A Finite Element Software for Multiphysical Problems
   Copyright 1st April 1995 - , CSC - IT Center for Science Ltd., Finland

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/***********************************************************************
Program:    ELMER Data base interface (EIO)
Author(s):  Harri Hakula 10.03.98
************************************************************************/

#ifndef EIOMODELMANAGER_H
#define EIOMODELMANAGER_H

#include "eio_config.h"


#include <typeinfo>
using namespace std;

#include <fstream>
#include <iostream>

class EIOModelManager
{
public:
  EIOModelManager();
  int createModel(const char *dir);
  int openModel(const char *dir);
  int closeModel();

  ~EIOModelManager();

  int openStream(fstream& fstr, const char *name, int mode);
  int closeStream(fstream& fstr);

  int makeDirectory(const char *dir);
  char *name() { return modelname; }
protected:
  char rundir[PATH_MAX];
  char modeldir[PATH_MAX];
  char modelname[PATH_MAX];

};

#endif  /* EIOMODELMANAGER_H */
