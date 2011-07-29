/*  
   ElmerGrid - A simple mesh generation and manipulation utility  
   Copyright (C) 1995- , CSC - IT Center for Science Ltd.   

   Author: Peter R�back
   Email: Peter.Raback@csc.fi
   Address: CSC - IT Center for Science Ltd.
            Keilaranta 14
            02101 Espoo, Finland

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

/* -------------------------------:  femelmer.c  :----------------------------
   This module includes interfaces for the other Elmer programs.
*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "nrutil.h"
#include "common.h"
#include "femdef.h"
#include "femtools.h"
#include "femtypes.h"
#include "femknot.h"
#include "femsolve.h"
#include "femelmer.h"
#include "../config.h"


#define getline fgets(line,MAXLINESIZE,in) 


int LoadSolutionElmer(struct FemType *data,int results,char *prefix,int info)
/* This procedure reads the solution in a form that is understood 
   by the programs Funcs and ElmerPost, created
   by Juha Ruokolainen at CSC - IT Center for Science Ltd.. 
   This procedure is not by far general.
   */
{
  int noknots,noelements,novctrs,elemcode,open;
  int timesteps,i,j,k,grp;
  Real r;
  FILE *in;
  char line[MAXLINESIZE],filename[MAXFILESIZE],text[MAXNAMESIZE];

  AddExtension(prefix,filename,"ep");
  if ((in = fopen(filename,"r")) == NULL) {
    printf("LoadSolutionElmer: The opening of the Elmer-file %s wasn't succesfull!\n",
	   filename);
    return(1);
  }
  else 
    printf("Loading Elmer data from %s\n",filename);

  InitializeKnots(data);

  getline;
  sscanf(line,"%d %d %d %d",&noknots,&noelements,&novctrs,&timesteps);

  data->dim = 3;
  data->maxnodes = MAXNODESD2;
  data->noknots = noknots;
  data->noelements = noelements;
  data->timesteps = timesteps;
  
  if(timesteps > 1) 
    printf("LoadSolutionElmer: The subroutine may crash with %d timesteps\n",
	   timesteps);
  if(timesteps < 1) timesteps = 1;
    
  if(info) printf("Allocating for %d knots and %d elements.\n",
		  noknots,noelements);
  AllocateKnots(data);

  if(results) {
    if(timesteps > 1) 
      data->times = Rvector(0,timesteps-1);
    for(i=1;i<=novctrs;i++) {
      sprintf(text,"var%d",i);
      CreateVariable(data,i,timesteps,0.0,text,FALSE);    
    }
  }

  if(info) printf("Reading %d coordinates.\n",noknots);
  for(i=1; i <= noknots; i++) {
    getline;
    sscanf(line,"%le %le %le",
	   &(data->x[i]),&(data->y[i]),&(data->z[i]));
  }

  if(info) printf("Reading %d element topologies.\n",noelements);

  grp = 0;
  open = FALSE;
  for(i=1; i <= noelements; i++) {
    fscanf(in,"%s",text);
    if(strstr(text,"#group")) {
      grp++;
      printf("Starting a new element group\n");
      fscanf(in,"%s",text);      
      fscanf(in,"%s",text);
      open = TRUE;
    }
    if(strstr(text,"#end")) {
      printf("Ending an element group\n");
      fscanf(in,"%s",text);      
      open = FALSE;
    }
    fscanf(in,"%d",&(data->elementtypes[i]));
    data->material[i] = grp;
    for(j=0;j< data->elementtypes[i]%100 ;j++) {
      k = fscanf(in,"%d",&(data->topology[i][j]));
      data->topology[i][j] += 1;
    }
  }
  if(open) {    
    do {
      fscanf(in,"%s",text);
    } while (!strstr(text,"#end"));
    fscanf(in,"%s",text);
    printf("Ending an element group\n");   
    open = FALSE;
  }

  if(results == 0) 
    return(0);

  if(info) printf("Reading %d degrees of freedom for %d knots.\n",
		  novctrs,noknots);
  if (timesteps<2) {
    for(i=1; i <= noknots; i++) 
      for(j=1;j <= novctrs;j++) 
	fscanf(in,"%le",&(data->dofs[j][i]));
  }
  else for(k=0;k<timesteps;k++) {
    i = fscanf(in,"%s",text);
    if(i < 0) goto end;
    fscanf(in,"%d",&i);
    fscanf(in,"%d",&j);
    fscanf(in,"%le",&r);

    if(0) printf("Loading steps i=%d  j=%d  k=%d  r=%.3lg\n",i,j,k,r);

    for(i=1; i <= noknots; i++) 
      for(j=1;j <= novctrs;j++) 
	fscanf(in,"%le",&(data->dofs[j][k*noknots+i]));
  }

end:
  data->timesteps = k+1;

  fclose(in);

  return(0);
}



int FuseSolutionElmerPartitioned(char *prefix,char *outfile,int decimals,int parts,
				 int minstep, int maxstep, int dstep, int info)
{
#define LONGLINE 1024
  int *noknots,*noelements,novctrs,elemcode,open;
  int totknots,totelements,sumknots,sumelements;
  int timesteps,i,j,k,l,step;
  int ind[MAXNODESD3];
  int nofiles,activestep;
  Real r, *res, x, y, z;
  FILE *in[MAXPARTITIONS+1],*intest,*out;
  char line[LONGLINE],filename[MAXFILESIZE],text[MAXNAMESIZE],outstyle[MAXFILESIZE];
  char *cp;

  if(minstep || maxstep || dstep) {
    if(info) printf("Saving results in the interval from %d to %d with step %d\n",minstep,maxstep,dstep);
  }

  for(i=0;;i++) {
    sprintf(filename,"%s.ep.%d",prefix,i);
    if ((intest = fopen(filename,"r")) == NULL) break;
    if(i<=MAXPARTITIONS) in[i] = intest;
  }
  if(i > MAXPARTITIONS) {
    printf("**********************************************************\n");
    printf("Only data for %d partitions is fused (%d)\n",MAXPARTITIONS,i);
    printf("**********************************************************\n");
    i = MAXPARTITIONS;
  } 
  nofiles = i;

  if(nofiles < 2) {
    printf("Opening of partitioned data from file %s wasn't succesfull!\n",
	   filename);
    return(2);
  } else {
    if(parts > 0) nofiles = MIN(parts,nofiles);
    if(info) printf("Loading Elmer results from %d partitions.\n",nofiles);
  }

  if(minstep || maxstep || dstep) {
    if(info) printf("Saving results in the interval from %d to %d with step %d\n",minstep,maxstep,dstep);
  }

  noknots = Ivector(0,nofiles-1);
  noelements = Ivector(0,nofiles-1);
 
  sumknots = 0;
  sumelements = 0;

  for(i=0;i<nofiles;i++) {
    fgets(line,LONGLINE,in[i]);
    if(i==0) {
      cp = line;
      noknots[i] = next_int(&cp);
      noelements[i] = next_int(&cp);
      novctrs = next_int(&cp);
      timesteps = next_int(&cp);
    }
    else {
      sscanf(line,"%d %d",&noknots[i],&noelements[i]);
    }
    sumknots += noknots[i];
    sumelements += noelements[i];
  }
  totknots = sumknots;
  totelements = sumelements;
  res = Rvector(1,novctrs);

  if(info) printf("There are altogether %d nodes and %d elements.\n",totknots,sumelements);


  AddExtension(outfile,filename,"ep");
  if(info) printf("Saving ElmerPost data to %s.\n",filename);  
  out = fopen(filename,"w");
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(3);
  }

  i = timesteps;
  if(minstep || maxstep || dstep) {
    if ( dstep>1 ) {
      i = 0;
      for(step = minstep; step <= maxstep; step++)
        if((step-minstep)%dstep==0) i++;
    } else i=maxstep-minstep+1;
  }
  fprintf(out,"%d %d %d %d %s %s",totknots,totelements,novctrs+1,i,"scalar: Partition",cp);
 
  if(info) printf("Reading and writing %d coordinates.\n",totknots);
  sprintf(outstyle,"%%.%dlg %%.%dlg %%.%dlg\n",decimals,decimals,decimals);

  for(j=0; j < nofiles; j++) {
    for(i=1; i <= noknots[j]; i++) {
      do {
	fgets(line,LONGLINE,in[j]);
      } while(line[0] == '#');

      sscanf(line,"%le %le %le",&x,&y,&z);
      fprintf(out,outstyle,x,y,z);
    }
  }

  if(info) printf("Reading and writing %d element topologies.\n",totelements);
  sumknots = 0;

  for(j=0; j < nofiles; j++) {
    open = FALSE;
    for(i=1; i <= noelements[j]; i++) {
      do {
	fgets(line,LONGLINE,in[j]);
      } while (line[0] == '#');

      sscanf(line,"%s",text);
      cp = strstr(line," ");

      elemcode = next_int(&cp);

      for(k=0;k< elemcode%100 ;k++) {
	/* Dirty trick for long lines */
	l = strspn(cp," ");
	if( l == 0) {
	  fgets(line,LONGLINE,in[j]);
	  cp = line;
	}
	ind[k] = next_int(&cp);
      }
      if(elemcode == 102) elemcode = 101;

      fprintf(out,"%s %d",text,elemcode);
      for(k=0;k < elemcode%100 ;k++)       
	fprintf(out," %d",ind[k]+sumknots);
      fprintf(out,"\n");
    }
    sumknots += noknots[j];
  }

  if(info) printf("Reading and writing %d degrees of freedom.\n",novctrs);
  sprintf(outstyle,"%%.%dlg ",decimals);

  activestep = FALSE;
  if(maxstep) timesteps = MIN(timesteps, maxstep);

  for(step = 1; step <= timesteps; step++) {
        
    if (step>=minstep) {
      if ( dstep>0 ) {
         activestep=((step-minstep)%dstep==0);
      } else activestep=TRUE;
    }

    for(k=0;k<nofiles;k++) 
      for(i=1; i <= noknots[k]; i++) {
	do {
	  fgets(line,LONGLINE,in[k]);
          if (activestep) {
            if(k==0 && strstr(line,"#time")) {
	      fprintf(out,"%s",line);
	      fprintf(stderr,"%s",line);
            }
          }
	}
	while (line[0] == '#');

	if(activestep) {
	  cp = line;
	  for(j=1;j <= novctrs;j++) 
	    res[j] = next_real(&cp);
	  
	  fprintf(out,"%d ",k+1);
	  for(j=1;j <= novctrs;j++) 
	    fprintf(out,outstyle,res[j]);
	  fprintf(out,"\n");
	}

      }
  }


  for(i=0;i<nofiles;i++) 
    fclose(in[i]);
  fclose(out);

  if(info) printf("Successfully fused partitioned Elmer results\n");

  return(0);
}



static int FindParentSide(struct FemType *data,struct BoundaryType *bound,
			  int sideelem,int sideelemtype,int *sideind)
{
  int i,j,k,sideelemtype2,elemind,parent,normal;
  int elemsides,side,sidenodes,nohits,hit,noparent, bulknodes;
  int sideind2[MAXNODESD1];

  hit = FALSE;

  for(parent=1;parent<=2;parent++) {
    if(parent == 1) {
      elemind = bound->parent[sideelem];
      noparent = (parent < 1);
    }
    else
      elemind = bound->parent2[sideelem];

    if(elemind > 0) {
      elemsides = data->elementtypes[elemind] / 100;
      bulknodes = data->elementtypes[elemind] % 100;

      if(elemsides == 8) elemsides = 6;
      else if(elemsides == 6) elemsides = 5;
      else if(elemsides == 5) elemsides = 4;
      
      for(normal=1;normal >= -1;normal -= 2) {

	for(side=0;side<elemsides;side++) {

	  GetElementSide(elemind,side,normal,data,&sideind2[0],&sideelemtype2);

	  if(sideelemtype2 < 300 && sideelemtype > 300) break;	
	  if(sideelemtype2 < 200 && sideelemtype > 200) break;		
	  if(sideelemtype != sideelemtype2) continue;

	  sidenodes = sideelemtype % 100;

	  for(j=0;j<sidenodes;j++) {
	    hit = TRUE;
	    for(i=0;i<sidenodes;i++) 
	      if(sideind[(i+j)%sidenodes] != sideind2[i]) hit = FALSE;
	    
	    if(hit == TRUE) {
	      if(parent == 1) {
		bound->side[sideelem] = side;
		bound->normal[sideelem] = normal;
	      }
	      else {
		bound->side2[sideelem] = side;	      
	      }
	      goto skip;
	    }
	  }
	}
      }	

      
      /* this finding of sides does not guarantee that normals are oriented correctly */
      normal = 1;
      hit = FALSE;
 
      for(side=0;;side++) {

	GetElementSide(elemind,side,normal,data,&sideind2[0],&sideelemtype2);

	if(sideelemtype2 < 300 && sideelemtype > 300) break;	
	if(sideelemtype2 < 200 && sideelemtype > 200) break;		
	if(sideelemtype != sideelemtype2) continue;
	
	sidenodes = sideelemtype % 100;

	nohits = 0;
	for(j=0;j<sidenodes;j++) 
	  for(i=0;i<sidenodes;i++) 
	    if(sideind[i] == sideind2[j]) nohits++;
	if(nohits == sidenodes) {
	  hit = TRUE;
	  if(parent == 1) {
	    bound->side[sideelem] = side;
	  }
	  else 
	    bound->side2[sideelem] = side;	      
	  goto skip;
	}
	
      }
    }

  skip:  
    if(!hit) {
      printf("FindParentSide: unsuccesfull (elemtype=%d elemsides=%d parent=%d)\n",
		    sideelemtype,elemsides,parent);

      printf("parents = %d %d\n",bound->parent[sideelem],bound->parent2[sideelem]);

      printf("sideind =");
      for(i=0;i<sideelemtype%100;i++)
      printf(" %d ",sideind[i]);
      printf("\n");

      printf("elemind =");
      for(i=0;i<elemsides;i++)
      printf(" %d ",data->topology[elemind][i]);
      printf("\n");      
    }

  }

  return(0);
}




int LoadElmerInput(struct FemType *data,struct BoundaryType *bound,
		   char *prefix,int info)
/* This procedure reads the mesh assuming ElmerSolver format.
   */
{
  int noknots,noelements,nosides,maxelemtype;
  int sideind[MAXNODESD1],tottypes,elementtype;
  int i,j,k,l,dummyint,cdstat;
  FILE *in;
  char line[MAXLINESIZE],filename[MAXFILESIZE],directoryname[MAXFILESIZE];


  sprintf(directoryname,"%s",prefix);
  cdstat = chdir(directoryname);

  if(info) {
    if(cdstat) 
      printf("Loading mesh in ElmerSolver format from root directory.\n");
    else
      printf("Loading mesh in ElmerSolver format from directory %s.\n",directoryname);
  }

  InitializeKnots(data);


  sprintf(filename,"%s","mesh.header");
  if ((in = fopen(filename,"r")) == NULL) {
    printf("LoadElmerInput: The opening of the header-file %s failed!\n",
	   filename);
    return(1);
  }
  else 
    printf("Loading header from %s\n",filename);

  getline;
  sscanf(line,"%d %d %d",&noknots,&noelements,&nosides);
  getline;
  sscanf(line,"%d",&tottypes);

  maxelemtype = 0;
  for(i=1;i<=tottypes;i++) {   
    getline;
    sscanf(line,"%d",&dummyint);
    if(dummyint > maxelemtype) maxelemtype = dummyint;
  }
  fclose(in);

  data->dim = GetElementDimension(maxelemtype);

  data->maxnodes = maxelemtype % 100;
  data->noknots = noknots;
  data->noelements = noelements;


  if(info) printf("Allocating for %d knots and %d elements.\n",
		  noknots,noelements);
  AllocateKnots(data);


  sprintf(filename,"%s","mesh.nodes");
  if ((in = fopen(filename,"r")) == NULL) {
    if(info) printf("LoadElmerInput: The opening of the nodes-file %s failed!\n",
		    filename);
    return(2);
  }
  else 
    printf("Loading %d Elmer nodes from %s\n",noknots,filename);

  for(i=1; i <= noknots; i++) {
    getline;
    sscanf(line,"%d %d %le %le %le",
	   &j, &dummyint, &(data->x[i]),&(data->y[i]),&(data->z[i]));
    if(j != i) printf("LoadElmerInput: nodes i=%d j=%d\n",i,j);
  }
  fclose(in);


  sprintf(filename,"%s","mesh.elements");
  if ((in = fopen(filename,"r")) == NULL) {
    printf("LoadElmerInput: The opening of the element-file %s failed!\n",
	   filename);
    return(3);
  }
  else 
    if(info) printf("Loading %d bulk elements from %s\n",noelements,filename);

  for(i=1; i <= noelements; i++) {
    fscanf(in,"%d",&j);
    if(0 && i != j) printf("LoadElmerInput: i=%d element=%d\n",i,dummyint);
    fscanf(in,"%d",&(data->material[j]));
    fscanf(in,"%d",&elementtype);
    if(elementtype > maxelemtype ) {
      printf("Invalid bulk elementtype: %d\n",elementtype);
      bigerror("Cannot continue with invalid elements");
    }
    data->elementtypes[j] = elementtype;
    for(k=0;k< elementtype%100 ;k++) {
      fscanf(in,"%d",&l);
      data->topology[j][k] = l;
      if(l < 0 || l > noknots ) {
	printf("node out of range: %d %d %d %d %d\n",i,j,elementtype,k,l);
      }
    }
  }
  fclose(in);


  sprintf(filename,"%s","mesh.boundary");
  if ((in = fopen(filename,"r")) == NULL) {
    printf("LoadElmerInput: The opening of the boundary-file %s failed!\n",
	   filename);
    return(4);
  }
  else {
    if(info) printf("Loading %d boundary elements from %s\n",nosides,filename);
  }

  AllocateBoundary(bound,nosides);
  data->noboundaries = 1;

  i = 0;
  for(k=1; k <= nosides; k++) {
    i++;
    fscanf(in,"%d",&dummyint);

#if 0
    if(k != dummyint) printf("LoadElmerInput: k=%d side=%d\n",k,dummyint);
#endif
    fscanf(in,"%d",&(bound->types[i]));
    fscanf(in,"%d",&(bound->parent[i]));
    fscanf(in,"%d",&(bound->parent2[i]));
    fscanf(in,"%d",&elementtype);

    if(elementtype > maxelemtype ) {
      printf("Invalid boundary elementtype: %d\n",elementtype);
      bigerror("Cannot continue with invalid elements");
    }
    for(j=0;j< elementtype%100 ;j++) 
      fscanf(in,"%d",&(sideind[j]));

    if(bound->parent[i] == 0 && bound->parent2[i] != 0) {
      bound->parent[i] = bound->parent2[i];
      bound->parent2[i] = 0;
    }

    if(bound->parent[i] > 0) {
      FindParentSide(data,bound,i,elementtype,sideind);
    }
    else {
#if 0
      printf("could not find parent for side %d with inds %d %d\n",
	     dummyint,sideind[0],sideind[1]);
      printf("eleminfo: parents %d %d type %d\n",
	     bound->parent[i],bound->parent2[i],bound->types[i]);   
#endif
      i--;
    }
  }
  
  bound->nosides = i;
  fclose(in); 

  if(!cdstat) chdir("..");

  printf("All done\n");

  return(0);
}




int SaveSolutionElmer(struct FemType *data,struct BoundaryType *bound,
		      int nobound,char *prefix,int decimals,int info)
/* This procedure saves the solution in a form that is understood 
   by the programs Funcs and ElmerPost, created
   by Juha Ruokolainen at CSC - IT Center for Science Ltd.. 
   */
{
  int material,noknots,noelements,bulkelems,novctrs,sideelems,sideelemtype,elemtype,boundtype;
  char filename[MAXFILESIZE],outstyle[MAXFILESIZE];
  int i,j,k,l,nodesd1,timesteps,nodesd2,fail;
  int ind[MAXNODESD1];
  Real *rpart;
  FILE *out;

  if(!data->created) {
    printf("SaveSolutionElmer: You tried to save points that were never created.\n");
    return(1);
  }

  /* Make a variable showing the owner partition */
  if(data->partitionexist) {
    l = 0;
    do l++; while (data->edofs[l]);
    CreateVariable(data,l,1,0.0,"Partition",FALSE);      
    rpart = data->dofs[l];
    for(i=1;i<=data->noknots;i++) 
      rpart[i] = 1.0 * data->nodepart[i];
  }

  if(data->variables == 0) {
    printf("SaveSolutionElmer: there are no dofs to save!\n");
    return(2);
  }
  
  sideelems = 0;
  if(nobound) {
    for(i=0;i<nobound;i++) {
      if(bound[i].created) sideelems += bound[i].nosides; 
    }
  }

  noknots = data->noknots;
  bulkelems = data->noelements;
  if(nobound)
    noelements = bulkelems + sideelems;
  else
    noelements = bulkelems;
  timesteps = data->timesteps;
  if(timesteps < 1) timesteps = 1;

  novctrs = 0;
  for(i=0;i<MAXDOFS;i++) {
    if(data->edofs[i] == 1) novctrs += 1; 
    if(data->edofs[i] == 2) novctrs += 3; 
    if(data->edofs[i] == 3) novctrs += 3; 
  }

  AddExtension(prefix,filename,"ep");
  if(info) printf("Saving ElmerPost data to %s.\n",filename);  

  out = fopen(filename,"w");
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(3);
  }

  fprintf(out,"%d %d %d %d",noknots,noelements,novctrs,timesteps);

  for(i=0; i<MAXDOFS; i++) {
    if(data->edofs[i] == 1) 
      fprintf(out," scalar: %s",data->dofname[i]);
    else if(data->edofs[i] > 1) 
      fprintf(out," vector: %s",data->dofname[i]);
  }
  fprintf(out,"\n");

  if(info) printf("Saving %d node coordinates.\n",noknots);
  
  if(data->dim == 1) {
    sprintf(outstyle,"%%.%dlg 0.0 0.0\n",decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,data->x[i]);
  }
  else if(data->dim == 2) {
    sprintf(outstyle,"%%.%dlg %%.%dlg 0.0\n",decimals,decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,data->x[i],data->y[i]);
  }
  else if(data->dim == 3) {
    sprintf(outstyle,"%%.%dlg %%.%dlg %%.%dlg\n",decimals,decimals,decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,data->x[i],data->y[i],data->z[i]);      
  }

  printf("Saving %d bulk element topologies.\n",bulkelems);

  for(i=1;i<=bulkelems;i++) {
    elemtype = data->elementtypes[i];
    material = data->material[i];

    if(data->bodynamesexist) 
      fprintf(out,"body_%d_%s %d ",material,data->bodyname[material],elemtype);
    else if(elemtype/100 > 4) 
      fprintf(out,"vol%d %d ",material,elemtype);
    else if(elemtype/100 > 2) 
      fprintf(out,"surf%d %d ",material,elemtype);
    else if(elemtype/100 > 1) 
      fprintf(out,"line%d %d ",material,elemtype);
    else 
      fprintf(out,"pnt%d %d ",material,elemtype);

    nodesd2 = data->elementtypes[i]%100;
    for(j=0;j<nodesd2;j++) 
      fprintf(out,"%d ",data->topology[i][j]-1);
    fprintf(out,"\n");    
  }

  if(nobound) {
    printf("Saving %d side element topologies.\n",sideelems);
    for(j=0;j<nobound;j++) {
      if(bound[j].created == FALSE) continue;
      
      for(i=1;i<=bound[j].nosides;i++) {

	if(1) 
	  GetBoundaryElement(i,&bound[j],data,ind,&sideelemtype); 
	else
	  GetElementSide(bound[j].parent[i],bound[j].side[i],bound[j].normal[i],data,ind,&sideelemtype); 

	boundtype = bound[j].types[i];

	if(data->boundarynamesexist) 
	  fprintf(out,"bc_%d_%s %d ",boundtype,data->boundaryname[boundtype],sideelemtype);	  
	else if(sideelemtype/100 > 2) 
	  fprintf(out,"bcside%d %d ",boundtype,sideelemtype);
	else if(sideelemtype/100 > 1) 
	  fprintf(out,"bcline%d %d ",boundtype,sideelemtype);
	else 
	fprintf(out,"bcpnt%d %d ",boundtype,sideelemtype);

	nodesd1 = sideelemtype%100;
	for(k=0;k<nodesd1;k++)
	  fprintf(out,"%d ",ind[k]-1);
	fprintf(out,"\n");
      }
    }
  }

  printf("Saving %d degrees of freedom for each knot.\n",novctrs);
  for(k=0;k<timesteps;k++) {
    for(i=1;i<=noknots;i++){
      for(j=0;j<MAXDOFS;j++) {
	if(data->edofs[j] == 1) 
	  fprintf(out,"%.6lg ",data->dofs[j][k*noknots+i]);
	if(data->edofs[j] == 2) 
	  fprintf(out,"%.6lg %.6lg 0.0 ",
		  data->dofs[j][2*(k*noknots+i)-1],data->dofs[j][2*(k*noknots+i)]);
	if(data->edofs[j] == 3) 
	  fprintf(out,"%.6lg %.6lg %.6lg ",
		  data->dofs[j][3*(k*noknots+i)-2],
		  data->dofs[j][3*(k*noknots+i)-1],
		  data->dofs[j][3*(k*noknots+i)]);
      }
      fprintf(out,"\n");
    }
  }
  fclose(out);

  return(0);
}


int SaveElmerInput(struct FemType *data,struct BoundaryType *bound,
		   char *prefix,int decimals,int info)
/* Saves the mesh in a form that may be used as input 
   in Elmer calculations. 
   */
#define MAXELEMENTTYPE 827
{
  int noknots,noelements,material,sumsides,elemtype,fail,connodes;
  int sideelemtype,conelemtype,nodesd1,nodesd2,newtype;
  int i,j,k,l,bulktypes[MAXELEMENTTYPE+1],sidetypes[MAXELEMENTTYPE+1];
  int alltypes[MAXELEMENTTYPE+1],tottypes;
  int ind[MAXNODESD1],ind2[MAXNODESD1],usedbody[MAXBODIES],usedbc[MAXBCS];
  FILE *out;
  char filename[MAXFILESIZE], outstyle[MAXFILESIZE];
  char directoryname[MAXFILESIZE];

  if(!data->created) {
    printf("You tried to save points that were never created.\n");
    return(1);
  }

  noelements = data->noelements;
  noknots = data->noknots;
  sumsides = 0;

  for(i=0;i<=MAXELEMENTTYPE;i++)
    alltypes[i] = bulktypes[i] = sidetypes[i] = 0;

  for(i=0;i<MAXBODIES;i++)
    usedbody[i] = 0;
  for(i=0;i<MAXBCS;i++)
    usedbc[i] = 0;

  sprintf(directoryname,"%s",prefix);

  if(info) printf("Saving mesh in ElmerSolver format to directory %s.\n",
		  directoryname);

  fail = chdir(directoryname);
  if(fail) {
#ifdef MINGW32
    fail = mkdir(directoryname);
#else
    fail = mkdir(directoryname,0700);
#endif
    if(fail) {
      printf("Could not create a result directory!\n");
      return(1);
    }
    else {
      chdir(directoryname);
    }
  }
  else {
    printf("Reusing an existing directory\n");
  }

  sprintf(filename,"%s","mesh.nodes");
  out = fopen(filename,"w");

  if(info) printf("Saving %d coordinates to %s.\n",noknots,filename);  
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(2);
  }

  connodes = 0;
  if(data->connectexist) {
    for(i=1;i<=data->noknots;i++) 
      connodes = MAX( connodes, data->connect[i]);
    if(info) printf("Creating %d new nodes for connectivity conditions\n",connodes);
  }
  
  if(data->dim == 1) {
    sprintf(outstyle,"%%d %%d %%.%dlg 0.0 0.0\n",decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,i,-1,data->x[i]);
    for(i=1; i <= connodes; i++) 
      fprintf(out,outstyle,noknots+i,-1,data->x[1]);
  }
  if(data->dim == 2) {
    sprintf(outstyle,"%%d %%d %%.%dlg %%.%dlg 0.0\n",decimals,decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,i,-1,data->x[i],data->y[i]);
    for(i=1; i <= connodes; i++) 
      fprintf(out,outstyle,noknots+i,-1,data->x[1],data->y[1]);
  }
  else if(data->dim == 3) {
    sprintf(outstyle,"%%d %%d %%.%dlg %%.%dlg %%.%dlg\n",decimals,decimals,decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,i,-1,data->x[i],data->y[i],data->z[i]);    
    for(i=1; i <= connodes; i++) 
      fprintf(out,outstyle,noknots+i,-1,data->x[1],data->y[1],data->z[1]);    
  }

  fclose(out);

  sprintf(filename,"%s","mesh.elements");
  out = fopen(filename,"w");
  if(info) printf("Saving %d element topologies to %s.\n",noelements,filename);
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(3);
  }

  for(i=1;i<=noelements;i++) {
    elemtype = data->elementtypes[i];
    material = data->material[i];

    if(material < MAXBODIES) usedbody[material] += 1;
    fprintf(out,"%d %d %d",i,material,elemtype);

    bulktypes[elemtype] += 1;
    nodesd2 = elemtype%100;
    for(j=0;j < nodesd2;j++) 
      fprintf(out," %d",data->topology[i][j]);
    fprintf(out,"\n");          
  }
  fclose(out);


  sprintf(filename,"%s","mesh.boundary");
  out = fopen(filename,"w");
  if(info) printf("Saving boundary elements to %s.\n",filename);
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(4);
  }

  sumsides = 0;


  /* Save normal boundaries */
  for(j=0;j < MAXBOUNDARIES;j++) {
    if(bound[j].created == FALSE) continue;
    if(bound[j].nosides == 0) continue;
    
    for(i=1; i <= bound[j].nosides; i++) {

      if(1)
	GetBoundaryElement(i,&bound[j],data,ind,&sideelemtype); 
      else
	GetElementSide(bound[j].parent[i],bound[j].side[i],bound[j].normal[i],data,ind,&sideelemtype); 
      sumsides++;
      
      fprintf(out,"%d %d %d %d ",
	      sumsides,bound[j].types[i],bound[j].parent[i],bound[j].parent2[i]);
      fprintf(out,"%d",sideelemtype);
      
      if(bound[j].types[i] < MAXBCS) usedbc[bound[j].types[i]] += 1;

      sidetypes[sideelemtype] += 1;
      nodesd1 = sideelemtype%100;
      for(l=0;l<nodesd1;l++)
	fprintf(out," %d",ind[l]);
      fprintf(out,"\n");
    }
  }


  /* Save additional connection arising from discontinuous boundaries */
  if(1) for(j=0;j < MAXBOUNDARIES;j++) {
    
    if(bound[j].created == FALSE) continue;
    if(bound[j].nosides == 0) continue;
    if(!bound[j].ediscont) continue;
    
    for(i=1; i <= bound[j].nosides; i++) {
      if(!bound[j].parent2[i] || !bound[j].discont[i]) continue;
      
      GetElementSide(bound[j].parent2[i],bound[j].side2[i],-bound[j].normal[i],data,ind2,&sideelemtype); 
      GetElementSide(bound[j].parent[i],bound[j].side[i],bound[j].normal[i],data,ind,&sideelemtype);       
      nodesd1 = sideelemtype%100;
      conelemtype = 100 + nodesd1 + 1;
      sidetypes[conelemtype] += nodesd1;
      
      for(k=0;k<nodesd1;k++) {
        sumsides++;
	    fprintf(out,"%d 0 0 0 %d %d ",sumsides,conelemtype,ind[k]);
	    for(l=0;l<nodesd1;l++)
	      fprintf(out,"%d ",ind2[l]);
	    fprintf(out,"\n");      
      }
    }
  }


  newtype = 0;
  for(j=0;j < MAXBOUNDARIES;j++) {
    if(bound[j].created == FALSE) continue;
    for(i=1; i <= bound[j].nosides; i++) 
      newtype = MAX(newtype, bound[j].types[i]);
  }
  

  if(data->connectexist) {
    int *connect,newsides,newline,count;
    connect = data->connect;
    
    for(k=1;;k++) {
      newsides = 0;
      for(i=1; i <= data->noknots; i++) 
	if(connect[i] == k) newsides++;
      if(newsides == 0) break;

      newtype++;      
      count = 0;

      if(info) printf("Adding %d connections to boundary condition %d\n",newsides,newtype);
      newline = sumsides;

      for(i=1; i <= data->noknots; i++) {
	if(connect[i] != k) continue;

	if(count == 0) {
	  if(newline != sumsides) fprintf(out,"\n");
	  newline = sumsides;
	  sumsides++;
	  count = MIN(63,newsides);	  
	  sideelemtype = 100 + count + 1;
	  sidetypes[sideelemtype] += 1;
	  fprintf(out,"%d %d %d %d %d %d",
		  sumsides,newtype,0,0,sideelemtype,data->noknots+k);
	  
	  if(0) printf("Added %d connection boundary conditions to boundary %d and elementtype %d.\n",
			  k,newtype,sideelemtype);
	  
	}	

	fprintf(out," %d",i);
	newsides--;
	count--;       
      }
      fprintf(out,"\n");
    }
  }

  fclose(out);

  tottypes = 0;
  for(i=0;i<=MAXELEMENTTYPE;i++) {
    alltypes[i] = bulktypes[i] + sidetypes[i];
    if(alltypes[i]) tottypes++;
  }

  sprintf(filename,"%s","mesh.header");
  out = fopen(filename,"w");
  if(info) printf("Saving header info to %s.\n",filename);  
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(4);
  }
  fprintf(out,"%-6d %-6d %-6d\n",
	  noknots+connodes,noelements,sumsides);
  fprintf(out,"%-6d\n",tottypes);
  for(i=0;i<=MAXELEMENTTYPE;i++) {
    if(alltypes[i]) 
      fprintf(out,"%-6d %-6d\n",i,bulktypes[i]+sidetypes[i]);
  }
  fclose(out);


  if(data->boundarynamesexist || data->bodynamesexist) {
    sprintf(filename,"%s","mesh.names");
    out = fopen(filename,"w");
    if(info) printf("Saving names info to %s.\n",filename);  
    if(out == NULL) {
      printf("opening of file was not successful\n");
      return(5);
    }
    
    if(data->bodynamesexist) {
      fprintf(out,"! ----- names for bodies -----\n");
      for(i=1;i<MAXBODIES;i++) 
	if(usedbody[i]) fprintf(out,"$ %s = %d\n",data->bodyname[i],i);
    }     
    if(data->boundarynamesexist) {
      fprintf(out,"! ----- names for boundaries -----\n");
      for(i=1;i<MAXBCS;i++) 
	if(usedbc[i]) fprintf(out,"$ %s = %d\n",data->boundaryname[i],i);
    }
    fclose(out);
  }
  
  chdir("..");
  
  return(0);
}



int SaveSizeInfo(struct FemType *data,struct BoundaryType *bound,
		 char *prefix,int info)
{
  int nosides;
  int i,j,k;
  FILE *out;
  char filename[MAXFILESIZE];

  if(!data->created) {
    printf("You tried to save points that were never created.\n");
    return(1);
  }

  nosides = 0;
  for(j=0;j < MAXBOUNDARIES;j++) {
    if(bound[j].created == FALSE) continue;
    nosides += bound[j].nosides;
  }

  AddExtension(prefix,filename,"size");
  if(info) printf("Saving size info into file %s.\n",filename);

  out = fopen(filename,"w");
  fprintf(out,"%d\n",data->noknots);
  fprintf(out,"%d\n",data->noelements);
  fprintf(out,"%d\n",nosides);
  fprintf(out,"%d\n",data->nopartitions);

  fclose(out);

  return(0);
}



int SaveElmerInputFemBem(struct FemType *data,struct BoundaryType *bound,
			 char *prefix,int decimals,int info)
/* Saves the mesh in a form that may be used as input 
   in Elmer calculations. Taylored to work with FEM/BEM coupling,
   or other problems with mixed dimension of bulk elements.
   */
{
  int noknots,noelements,material,sumsides,elemtype,fail,nobulkelements,bctype;
  int sideelemtype,nodesd1,nodesd2,newtype,elemdim,maxelemdim;
  int i,j,k,l,bulktypes[MAXELEMENTTYPE+1],sidetypes[MAXELEMENTTYPE+1],tottypes;
  int ind[MAXNODESD1],ind2[MAXNODESD1],bodyperm[MAXBODIES],bcperm[MAXBCS];
  FILE *out,*out2;
  char filename[MAXFILESIZE], outstyle[MAXFILESIZE];
  char directoryname[MAXFILESIZE];

  if(!data->created) {
    printf("You tried to save points that were never created.\n");
    return(1);
  }

  if(data->connectexist) smallerror("Connectivity data is not saved in the FEM/BEM version");
  if(data->nopartitions > 1) smallerror("Partitioning data is not saved in the FEM/BEM version");

  noelements = data->noelements;
  noknots = data->noknots;
  sumsides = 0;

  for(i=0;i<=MAXELEMENTTYPE;i++)
    bulktypes[i] = sidetypes[i] = 0;

  sprintf(directoryname,"%s",prefix);

  if(info) printf("Saving mesh in ElmerSolver format to directory %s.\n",
		  directoryname);

  fail = chdir(directoryname);
  if(fail) {
#ifdef MINGW32
    fail = mkdir(directoryname);
#else
    fail = mkdir(directoryname,0700);
#endif
    if(fail) {
      printf("Could not create a result directory!\n");
      return(1);
    }
    else {
      chdir(directoryname);
    }
  }
  else {
    printf("Reusing an existing directory\n");
  }

  sprintf(filename,"%s","mesh.nodes");
  out = fopen(filename,"w");

  if(info) printf("Saving %d coordinates to %s.\n",noknots,filename);  
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(2);
  }

  if(data->dim == 1) {
    sprintf(outstyle,"%%d %%d %%.%dlg 0.0 0.0\n",decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,i,-1,data->x[i]);
  }
  if(data->dim == 2) {
    sprintf(outstyle,"%%d %%d %%.%dlg %%.%dlg 0.0\n",decimals,decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,i,-1,data->x[i],data->y[i]);
  }
  else if(data->dim == 3) {
    sprintf(outstyle,"%%d %%d %%.%dlg %%.%dlg %%.%dlg\n",decimals,decimals,decimals);
    for(i=1; i <= noknots; i++) 
      fprintf(out,outstyle,i,-1,data->x[i],data->y[i],data->z[i]);    
  }
  fclose(out);


  sprintf(filename,"%s","mesh.elements");
  out = fopen(filename,"w");
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(3);
  }

  maxelemdim = GetMaxElementDimension(data);
  nobulkelements = 0;

  for(i=0;i<MAXBODIES;i++) bodyperm[i] = FALSE;
  for(i=1;i<=noelements;i++) {
    elemtype = data->elementtypes[i];
    elemdim = GetElementDimension(elemtype);
    material = data->material[i];

    if(elemdim == maxelemdim) 
      bodyperm[material] = 1;
    else 
      bodyperm[material] = -1;      
  }
  j = 0;
  k = 0;
  for(i=0;i<MAXBODIES;i++) {
    if(bodyperm[i] > 0) bodyperm[i] = ++j;
    if(bodyperm[i] < 0) bodyperm[i] = --k;
  }


  for(i=1;i<=noelements;i++) {
    elemtype = data->elementtypes[i];

    elemdim = GetElementDimension(elemtype);
    if(elemdim < maxelemdim) continue;

    nobulkelements++;
    material = data->material[i];
    material = bodyperm[material];
    fprintf(out,"%d %d %d",i,material,elemtype);
    
    bulktypes[elemtype] += 1;
    nodesd2 = elemtype%100;
    for(j=0;j < nodesd2;j++) 
      fprintf(out," %d",data->topology[i][j]);
    fprintf(out,"\n");          
  }
  fclose(out);

  if(info) printf("Saving %d (of %d) body elements to mesh.boundary\n",nobulkelements,noelements);


  sprintf(filename,"%s","mesh.boundary");
  out = fopen(filename,"w");
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(4);
  }

  sumsides = 0;
  newtype = 0;
  for(i=0;i<MAXBCS;i++) bcperm[i] = 0;
  for(j=0;j < MAXBOUNDARIES;j++) {
    if(bound[j].created == FALSE) continue;
    for(i=1; i <= bound[j].nosides; i++) 
      bcperm[bound[j].types[i]] = TRUE;
  }
  j = 0;
  for(i=0;i<MAXBCS;i++) 
    if(bcperm[i]) bcperm[i] = ++j;
  newtype = j;


  /* Save normal boundaries */
  for(j=0;j < MAXBOUNDARIES;j++) {
    if(bound[j].created == FALSE) continue;
    if(bound[j].nosides == 0) continue;
    
    for(i=1; i <= bound[j].nosides; i++) {
      GetElementSide(bound[j].parent[i],bound[j].side[i],bound[j].normal[i],data,ind,&sideelemtype); 
      sumsides++;
      material = bound[j].types[i];
      material = bcperm[material];
      
      fprintf(out,"%d %d %d %d %d",
	      sumsides,material,bound[j].parent[i],bound[j].parent2[i],sideelemtype);
      
      sidetypes[sideelemtype] += 1;

      nodesd1 = sideelemtype % 100;
      for(l=0;l<nodesd1;l++)
	fprintf(out," %d",ind[l]);
      fprintf(out,"\n");
    }
  }

  


  for(i=1;i<=noelements;i++) {
    elemtype = data->elementtypes[i];

    elemdim = GetElementDimension(elemtype);
    if(elemdim == maxelemdim) continue;

    sumsides++;

    material = data->material[i];
    bctype = abs(bodyperm[material]) + newtype;
    
    fprintf(out,"%d %d 0 0 %d",sumsides,bctype,elemtype);
    sidetypes[elemtype] += 1;
    nodesd1 = elemtype%100;
    for(l=0;l<nodesd1;l++)
      fprintf(out," %d",data->topology[i][l]);
    fprintf(out,"\n");
  }

  fclose(out);
  if(info) printf("Saving %d boundary elements to mesh.boundary\n",sumsides);


  tottypes = 0;
  for(i=0;i<=MAXELEMENTTYPE;i++) 
    if(bulktypes[i] || sidetypes[i]) tottypes++;

  sprintf(filename,"%s","mesh.header");
  out = fopen(filename,"w");
  if(info) printf("Saving header info with %d elementtypes to %s.\n",tottypes,filename);  
  if(out == NULL) {
    printf("opening of file was not successful\n");
    return(4);
  }
  fprintf(out,"%-6d %-6d %-6d\n",
	  noknots,nobulkelements,sumsides);
  fprintf(out,"%-6d\n",tottypes);
  for(i=0;i<=MAXELEMENTTYPE;i++) 
    if(bulktypes[i] || sidetypes[i]) 
      fprintf(out,"%-6d %-6d\n",i,bulktypes[i]+sidetypes[i]);
  fclose(out);

  if(info) printf("Body and boundary numbers were permutated\n");

  if(data->boundarynamesexist || data->bodynamesexist) {
    sprintf(filename,"%s","mesh.names");
    out = fopen(filename,"w");
    if(info) printf("Saving names info to %s.\n",filename);  
    if(out == NULL) {
      printf("opening of file was not successful\n");
      return(5);
    }
    
    if(data->bodynamesexist) {
      fprintf(out,"! ----- names for bodies -----\n");
      for(i=1;i<MAXBODIES;i++) 
	if(bodyperm[i] > 0) fprintf(out,"$ %s = %d\n",data->bodyname[i],bodyperm[i]);
    }     
    if(data->boundarynamesexist) {
      fprintf(out,"! ----- names for boundaries -----\n");
      /* The reordered numbering of the original BCs */
      for(i=1;i<MAXBCS;i++) {
	if(bcperm[i]) 
	  fprintf(out,"$ %s = %d\n",data->boundaryname[i],bcperm[i]);
      }
      /* Numbering of bodies that are actually saved as boundaries */
      for(i=1;i<MAXBODIES;i++) 
	if(bodyperm[i] < 0) 
	  fprintf(out,"$ %s = %d\n",data->bodyname[i],abs(bodyperm[i])+newtype);
    }
    fclose(out);
  }
  
  chdir("..");
  
  return(0);
}




int ElmerToElmerMapQuick(struct FemType *data1,struct FemType *data2,
			 char *mapfile,int info)
/* Requires that the mapping matrix is provided in a external file. */
{
  int i,j,k,l,idx,mink,maxk,unknowns;
  Real weight;
  FILE *out;

  if ((out = fopen(mapfile,"r")) == NULL) {
    printf("The opening of the mapping file %s wasn't succesfull!\n",
	   mapfile);
    return(1);
  }

  if(info) printf("Mapping results utilizing matrix in file %s.\n",mapfile);

  mink = MAXDOFS-1;
  maxk = 1;
  for(k=1;k<MAXDOFS;k++)
    if(data1->edofs[k]) {
      if(k < mink) mink = k;
      if(k > maxk) maxk = k;
      CreateVariable(data2,k,data1->edofs[k],
		     0.0,data1->dofname[k],FALSE);
    }

  for(j=1;j <= data2->noknots;j++) {
    fscanf(out,"%d",&idx);
    for(i=0;i<4;i++) {
      fscanf(out,"%d",&idx);
      fscanf(out,"%le",&weight);
      for(k=mink;k <= maxk;k++) 
	if(unknowns = data1->edofs[k]) {
	  for(l=1;l<=unknowns;l++)
	    data2->dofs[k][unknowns*(j-1)+l] += 
	      weight * data1->dofs[k][unknowns*(idx-1)+l];
	}
    }
  }
  return(0);
}


int ElmerToElmerMap(struct FemType *data1,struct FemType *data2,int info)
/* Maps Elmer results to another Elmer file. 
   Does not need the mapping a'priori. 
   */
{
  Real x1,x2,y1,y2;
  Real *xmin,*xmax;
  Real *ymin,*ymax;
  Real eta,xi;
  Real shapefunc1[MAXNODESD2],shapeder1[DIM*MAXNODESD2];
  Real coord1[MAXNODESD2],tiny;
  int elemno,ind1[MAXNODESD2];
  int *ymaxi;
  int i1,i2,j1,j2,hit,i,j,k,l;
  int mink,maxk,unknowns;
  int noelems1,noknots2;
  int material1;
  long tests;

  tests = 0;

  if(info) printf("Performing Elmer to Elmer mapping.\n");

  noelems1 = data1->noelements;
  noknots2 = data2->noknots;

  mink = MAXDOFS-1;
  maxk = 1;
  for(k=1;k<MAXDOFS;k++)
    if(data1->edofs[k]) {
      if(k < mink) mink = k;
      if(k > maxk) maxk = k;
      CreateVariable(data2,k,data1->edofs[k],
		     0.0,data1->dofname[k],FALSE);
    }

  xmin = Rvector(1,noelems1);
  xmax = Rvector(1,noelems1);
  ymin = Rvector(1,noelems1);
  ymax = Rvector(1,noelems1);

  ymaxi = ivector(1,noelems1);

  for(j1=1;j1<=noelems1;j1++) {
    xmax[j1] = xmin[j1] = data1->x[data1->topology[j1][0]];
    ymax[j1] = ymin[j1] = data1->y[data1->topology[j1][0]];

    for(i1=1;i1<4;i1++) {
      x1 = data1->x[data1->topology[j1][i1]];
      if (x1 < xmin[j1]) xmin[j1] = x1;
      if (x1 > xmax[j1]) xmax[j1] = x1;

      y1 = data1->y[data1->topology[j1][i1]];
      if (y1 < ymin[j1]) ymin[j1] = y1;
      if (y1 > ymax[j1]) ymax[j1] = y1;
    }
  }

  /* ymaxi must be ordered so that it points to the elements of 
     ymax in increasing order. In rectangular structures mesh 
     this is automatically the case. */
  if(info) printf("Ordering elements\n");
  for(j1=1;j1<=noelems1;j1++)
    ymaxi[j1] = j1;
#if 0
  /* This does not seem to function as intended. */
  indexx(noelems1,ymax,ymaxi);
#endif
  tiny = 1.0e-10*fabs(ymax[1]-ymax[noelems1]);

  j1 = 1;

  for(j2=1;j2<=noknots2;j2++) {

    x2 = data2->x[j2];
    y2 = data2->y[j2];

    /* Find first possible element using xmax */
    while(j1<noelems1 && ymax[ymaxi[j1]] < y2-tiny) 
      {j1++; tests++;} 
    while(j1>1 && ymax[ymaxi[j1]-1] > y2-tiny) 
      {j1--; tests++;}
    
  omstart:
    
    hit = FALSE;
    do {
      tests++;
      if(j1 > noelems1) break;
      elemno = ymaxi[j1];

      if(ymin[elemno] > y2+tiny) break;

      if(xmax[elemno] > x2-tiny  &&  xmin[elemno] < x2+tiny) 
	hit = TRUE;
      else 
	j1++;
      if(j1 > noelems1) break;
    }
    while (hit == FALSE);

    if(hit == FALSE) {
      if(j1 > noelems1) j1=noelems1;
      printf("No hits for element %d at (%.3lg,%.3lg)\n",j2,x2,y2);
      printf("j1 = %d  noelems1 = %d\n",j1,noelems1);
    }
    else {
      GetElementInfo(j1,data1,coord1,ind1,&material1);
      hit = GlobalToLocalD2(coord1,x2,y2,&xi,&eta);
      if(hit == FALSE) {
	j1++;
	goto omstart;
      }
      else {
	Squad404(&xi,&eta,shapefunc1,shapeder1);
	for(k=mink;k <= maxk;k++) 
	  if(unknowns = data1->edofs[k]) {
	    for(l=1;l<=unknowns;l++)
	      for(i=0;i<4;i++)
		data2->dofs[k][unknowns*(j2-1)+l] += 
		  shapefunc1[i] * data1->dofs[k][unknowns*(ind1[i]-1)+l];
	  }
      }
    }
  }
  if(info) printf("Mapped %d knots with %.3lg average trials.\n",
		  noknots2,(1.0*tests)/noknots2);

  return(0);
}




static int CreatePartitionTable(struct FemType *data,int info)
{
  int i,j,k,l,m,noelements,noknots,partitions,nonodes,periodic;
  int maxneededtimes,sharings,part,ind,hit,notinany,debug;
  int *indxper;

  printf("Creating a table showing all parenting partitions of nodes.\n");  

  if(data->maxpartitiontable) {
    printf("The partition table already exists!\n");
    smallerror("Partition table not done");
  }

  partitions = data->nopartitions;
  noelements = data->noelements;
  periodic = data->periodicexist;
  noknots = data->noknots;
  if(periodic) indxper = data->periodic;

  maxneededtimes = 0;
  sharings = 0;

  /* Make the partition list taking into account periodicity */
  for(i=1;i<=noelements;i++) {
    part = data->elempart[i];
    nonodes =  data->elementtypes[i] % 100;

    for(j=0;j < nonodes;j++) {
      ind = data->topology[i][j];
      if(periodic) ind = indxper[ind];

      debug = FALSE;
      if(debug) printf("ind=%d i=%d j=%d part=%d\n",ind,i,j,part); 

      hit = 0;
      for(k=1;k<=maxneededtimes;k++) { 
	if(data->partitiontable[k][ind] == 0) hit = k;
	if(data->partitiontable[k][ind] == part) hit = -k;
	if(hit) break;
      }
      if( hit > 0) {
	data->partitiontable[hit][ind] = part;
	if(hit == 2) sharings++;
      }
      else if(hit == 0) {
	maxneededtimes++;
	data->partitiontable[maxneededtimes] = Ivector(1,noknots);
	for(m=1;m<=noknots;m++)
	  data->partitiontable[maxneededtimes][m] = 0;
	data->partitiontable[maxneededtimes][ind] = part;
	if(maxneededtimes == 2) sharings++;
      }
      if(debug) printf("hit = %d\n",hit);
    }
  }


  /* Make the partitiontable such that the owner node is the first one in the list */  
  notinany = 0;
  for(i=1;i<=noknots;i++) {
    
    /* Skip the periodic nodes and take care of them later */
    if(periodic) 
      if(i != indxper[i]) continue;

    hit = FALSE;
    for(k=1;k<=maxneededtimes;k++) {
      if(!data->partitiontable[k][i]) break;
      if(data->partitiontable[k][i] == data->nodepart[i]) {
	hit = k;
	break;
      }
    }
    if( hit > 1 ) {
      data->partitiontable[hit][i] = data->partitiontable[1][i];
      data->partitiontable[1][i] = data->nodepart[i];
    }
    else if(!hit) {
      if(0) {
	printf("Node %d in partition %d not in the table!\n",i,data->nodepart[i]);
	if(periodic) printf("indexper: %d\n",indxper[i]);
      }
      
      notinany++;
      data->nodepart[i] = data->partitiontable[1][i];
    }
  }


  /* For periodic counterparts copy the table and ownership */
  if(periodic) {
    for(i=1;i<=noknots;i++) {
      ind = indxper[i];
      if(ind == i) continue;
      for(k=1;k<=maxneededtimes;k++) {
	if( !data->partitiontable[k][ind] ) break;
	data->partitiontable[k][i] = data->partitiontable[k][ind];
      }
      data->nodepart[i] = data->nodepart[ind];
    }
  }


  if(info) {
    printf("Nodes belong to %d partitions in maximum\n",maxneededtimes);
    printf("There are %d shared nodes which is %.2lf %% of all nodes.\n",
	   sharings,(100.*sharings)/noknots);
    printf("The initial owner was not any of the elements for %d nodes\n",notinany);
  }


  if(0) for(i=1;i<=noknots;i++) {
    if(data->partitiontable[maxneededtimes][i]) {
      printf("node %d parts: ",i);
      for(k=1;k<=maxneededtimes;k++) 
	printf("%d ",data->partitiontable[k][i]);
      printf("\n");
    }
  }
    
  data->maxpartitiontable = maxneededtimes;
  data->partitiontableexists = TRUE;
  return(0);
}



static int PartitionElementsByNodes(struct FemType *data,int info)
{
  int i,j,k,noknots,nonodes,noelements,nopartitions,part,maxpart,maxpart2,minpart;
  int *elempart,*nodepart,*nodesinpart,*cuminpart,**knows,**cumknows,set;

  if(!data->partitionexist) return(1);

  noknots = data->noknots;
  noelements = data->noelements;
  nopartitions = data->nopartitions;
  elempart = data->elempart;
  nodepart = data->nodepart;

  nodesinpart = Ivector(1,nopartitions);
  cuminpart = Ivector(1,nopartitions);
  for(j=1;j<=nopartitions;j++) 
    cuminpart[j] = 0;

  knows = Imatrix(1,nopartitions,1,nopartitions);
  cumknows = Imatrix(1,nopartitions,1,nopartitions);
  for(i=1;i<=nopartitions;i++)
    for(j=1;j<=nopartitions;j++)
      knows[i][j] = cumknows[i][j] = 0;

  set = FALSE;

 omstart:

  /* In the first round count the equally joined elements and 
     on the second round split them equally using cumulative numbering */

  for(i=1;i<=noelements;i++) {
    for(j=1;j<=nopartitions;j++) 
      nodesinpart[j] = 0;
    for(j=0;j<data->elementtypes[i] % 100;j++) {
      part = nodepart[data->topology[i][j]];
      nodesinpart[part] += 1;
    }
    maxpart = maxpart2 = 1;
    for(j=1;j<=nopartitions;j++) 
      if(nodesinpart[j] > nodesinpart[maxpart]) maxpart = j;
    if(maxpart == 1) maxpart2 = 2;
    for(j=1;j<=nopartitions;j++) {
      if(j == maxpart) continue;
      if(nodesinpart[j] > nodesinpart[maxpart2]) maxpart2 = j;
    }
    
    if(nodesinpart[maxpart] > nodesinpart[maxpart2]) {
      if(set) 
	elempart[i] = maxpart;    
      else
	cuminpart[maxpart] += 1;
    }
    else {
      if(set) {
	cumknows[maxpart][maxpart2] += 1;
	if( cumknows[maxpart][maxpart2] > knows[maxpart][maxpart2] / 2) {
	  elempart[i] = maxpart2;
	  cuminpart[maxpart2] += 1;
	}
	else {
	  elempart[i] = maxpart;
	  cuminpart[maxpart] += 1;
	}
      }	
      else
	knows[maxpart][maxpart2] += 1;
    }
  }    

  if(!set) {
    set = TRUE;
    goto omstart;
  }

  minpart = maxpart = cuminpart[1];
  for(j=1;j<=nopartitions;j++) {
    minpart = MIN( minpart, cuminpart[j]);
    maxpart = MAX( maxpart, cuminpart[j]);
  }

  if(info) {
    printf("Set the element partitions by the dominating nodal partition\n");
    printf("There are from %d to %d elements in the %d partitions.\n",minpart,maxpart,nopartitions);
  }  

  free_Ivector(nodesinpart,1,nopartitions);
  free_Ivector(cuminpart,1,nopartitions);
  free_Imatrix(knows,1,nopartitions,1,nopartitions);
  free_Imatrix(cumknows,1,nopartitions,1,nopartitions);

  return(0);
}


static int PartitionNodesByElements(struct FemType *data,int info)
{
  int i,j,k,noknots,nonodes,noelements,nopartitions,part,minpart,maxpart;
  int maxpart2,*cuminpart,**knows,**cumknows,set;
  int *elempart,*nodepart,*nodesinpart;

  if(!data->partitionexist) return(1);

  CreateInverseTopology(data, info);

  noknots = data->noknots;
  noelements = data->noelements;
  nopartitions = data->nopartitions;
  elempart = data->elempart;
  nodepart = data->nodepart;

  nodesinpart = Ivector(1,nopartitions);
  cuminpart = Ivector(1,nopartitions);
  for(j=1;j<=nopartitions;j++) 
    cuminpart[j] = 0;

  knows = Imatrix(1,nopartitions,1,nopartitions);
  cumknows = Imatrix(1,nopartitions,1,nopartitions);
  for(i=1;i<=nopartitions;i++)
    for(j=1;j<=nopartitions;j++)
      knows[i][j] = cumknows[i][j] = 0;

  set = FALSE;
  
 omstart:

  for(i=1;i<=noknots;i++) {

    for(j=1;j<=nopartitions;j++) 
      nodesinpart[j] = 0;

    for(j=1;j<=data->maxinvtopo;j++) {
      k = data->invtopo[j][i];
      if(!k) break;
      part = elempart[k];
      nodesinpart[part] += 1;
    }
    
    /* Find the partition with maximum number of hits */
    maxpart = maxpart2 = 1;
    for(j=1;j<=nopartitions;j++) 
      if(nodesinpart[j] > nodesinpart[maxpart]) maxpart = j;

    /* Find the partition with 2nd largest number of hits */
    if(maxpart == 1) maxpart2 = 2;
    for(j=1;j<=nopartitions;j++) {
      if(j == maxpart) continue;
      if(nodesinpart[j] > nodesinpart[maxpart2]) maxpart2 = j;
    }

    /* If there is a clear dominator use that */
    if(nodesinpart[maxpart] > nodesinpart[maxpart2]) {
      if(set) 
	nodepart[i] = maxpart;    
      else
	cuminpart[maxpart] += 1;
    }

    /* Otherwise make a half and half split betwen the major owners */
    else {
      if(set) {
	cumknows[maxpart][maxpart2] += 1;
	if( cumknows[maxpart][maxpart2] > knows[maxpart][maxpart2] / 2) {
	  nodepart[i] = maxpart2;
	  cuminpart[maxpart2] += 1;
	}
	else {
	  nodepart[i] = maxpart;
	  cuminpart[maxpart] += 1;
	}
      }	
      else
	knows[maxpart][maxpart2] += 1;
    }
  }    
  
  if(!set) {
    set = TRUE;
    goto omstart;
  }

  minpart = maxpart = cuminpart[1];
  for(j=1;j<=nopartitions;j++) {
    minpart = MIN( minpart, cuminpart[j]);
    maxpart = MAX( maxpart, cuminpart[j]);
  }

  if(info) {
    printf("Set the node partitions by the dominating element partition.\n");
    printf("There are from %d to %d nodes in the %d partitions.\n",minpart,maxpart,nopartitions);
  }  

  free_Ivector(nodesinpart,1,nopartitions);
  free_Ivector(cuminpart,1,nopartitions);
  free_Imatrix(knows,1,nopartitions,1,nopartitions);
  free_Imatrix(cumknows,1,nopartitions,1,nopartitions);

  return(0);
}


static int PartitionNodesByElements2(struct FemType *data,int info)
{
  int i,j,k,noknots,nonodes,noelements,nopartitions,part,minpart,maxpart,elemtype,ind;
  int *elempart,*nodepart,*nodesinpart;

  if(!data->partitionexist) return(1);


  noknots = data->noknots;
  noelements = data->noelements;
  nopartitions = data->nopartitions;
  elempart = data->elempart;
  nodepart = data->nodepart;

  for(i=1;i<=noknots;i++)
    nodepart[i] = 0;

  for(j=1;j<=noelements;j++) {
    elemtype = data->elementtypes[j];
    nonodes = elemtype % 100;
    part = elempart[j];
    for(i=0;i<nonodes;i++) {
      ind = data->topology[j][i];
      if(nodepart[ind] == 0 || nodepart[ind] > part)
	nodepart[ind] = part;
    }
  }

  nodesinpart = Ivector(1,nopartitions);
  for(j=1;j<=nopartitions;j++) 
    nodesinpart[j] = 0;
  for(i=1;i<=noknots;i++) {
    part = nodepart[i];
    nodesinpart[part] += 1;
  }
  minpart = maxpart = nodesinpart[1];
  for(j=1;j<=nopartitions;j++) {
    minpart = MIN( minpart, nodesinpart[j]);
    maxpart = MAX( maxpart, nodesinpart[j]);
  }

  if(info) {
    printf("Set the node partitions by the smallest element partition.\n");
    printf("There are from %d to %d nodes in the %d partitions.\n",minpart,maxpart,nopartitions);
  }  

  free_Ivector(nodesinpart,1,nopartitions);
  return(0);
}



int PartitionSimpleElements(struct FemType *data,int dimpart[],int dimper[],
			    int partorder, Real corder[],int info)
{
  int i,j,k,ind,minpart,maxpart;
  int noknots, noelements,nonodes,elemsinpart,periodic;
  int partitions1,partitions2,partitions3,partitions;
  int vpartitions1,vpartitions2,vpartitions3,vpartitions;
  int *indx,*nopart,*inpart;
  Real *arrange;
  Real x,y,z,cx,cy,cz;
  
  partitions1 = dimpart[0];
  partitions2 = dimpart[1];
  partitions3 = dimpart[2];
  if(data->dim < 3) partitions3 = 1;
  partitions = partitions1 * partitions2 * partitions3;

  if(partitions1 < 2 && partitions2 < 2 && partitions3 < 2) {
    printf("Some of the divisions must be larger than one: %d %d %d\n",
	   partitions1, partitions2, partitions3 );
    bigerror("Partitioning not performed");
  }

  if(partitions >= data->noelements) {
    printf("There must be fever partitions than elements (%d vs %d)!\n",
	   partitions,data->noelements);
    bigerror("Partitioning not performed");
  }
    
  if(!data->partitionexist) {
    data->partitionexist = TRUE;
    data->elempart = Ivector(1,data->noelements);
    data->nodepart = Ivector(1,data->noknots);
    data->nopartitions = partitions;
  }
  inpart = data->elempart;
  
  vpartitions1 = partitions1;
  vpartitions2 = partitions2;
  vpartitions3 = partitions3;

  periodic = dimper[0] || dimper[1] || dimper[2];
  if(periodic) {
    if(dimper[0] && partitions1 > 1) vpartitions1 *= 2;
    if(dimper[1] && partitions2 > 1) vpartitions2 *= 2;
    if(dimper[2] && partitions3 > 1) vpartitions3 *= 2;
  }
  vpartitions = vpartitions1 * vpartitions2 * vpartitions3;
  nopart = Ivector(1,vpartitions);
  noelements = data->noelements;
  noknots = data->noknots;

  if(info) printf("Making a simple partitioning for %d elements in %d-dimensions.\n",
		  noelements,data->dim);

  arrange = Rvector(1,noelements);
  indx = Ivector(1,noelements);

  if(partorder) {
    cx = corder[0];
    cy = corder[1];
    cz = corder[2];    
  }
  else {
    cx = 1.0;
    cy = 0.0001;
    cz = cy*cy;
  }
  z = 0.0;

  for(i=1;i<=noelements;i++) 
    inpart[i] = 1;
  
  if(vpartitions1 > 1) {

    if(info) printf("Ordering 1st direction with (%.3lg*x + %.3lg*y + %.3lg*z)\n",cx,cy,cz);

    for(j=1;j<=noelements;j++) {
      nonodes = data->elementtypes[j]%100;
      x = y = z = 0.0;
      for(i=0;i<nonodes;i++) {
	k = data->topology[j][i];
	x += data->x[k];
	y += data->y[k];
	if(data->dim==3) z += data->z[k];
      }
      arrange[j] = (cx*x + cy*y + cz*z) / nonodes;
    }

    SortIndex(noelements,arrange,indx);

    for(i=1;i<=noelements;i++) {
      ind = indx[i];
      k = (i*vpartitions1-1)/noelements+1;
      inpart[ind] = k;
    }
  } 

 
  /* Partition in the 2nd direction taking into account the 1st direction */
  if(vpartitions2 > 1) {
    if(info) printf("Ordering in the 2nd direction.\n");

    for(j=1;j<=noelements;j++) {
      nonodes = data->elementtypes[j]%100;
      x = y = z = 0.0;
      for(i=0;i<nonodes;i++) {
	k = data->topology[j][i];
	x += data->x[k];
	y += data->y[k];
	if(data->dim==3) z += data->z[k];
      }
      arrange[j] = (-cy*x + cx*y + cz*z) / nonodes;
    }
    SortIndex(noelements,arrange,indx);
    
    for(i=1;i<=vpartitions;i++)
      nopart[i] = 0;
    
    elemsinpart = noelements / (vpartitions1*vpartitions2);
    for(i=1;i<=noelements;i++) {
      j = 0;
      ind = indx[i];
      do {
	j++;
	k = (inpart[ind]-1) * vpartitions2 + j;
      }
      while(nopart[k] >= elemsinpart && j < vpartitions2);
      
      nopart[k] += 1;
      inpart[ind] = (inpart[ind]-1)*vpartitions2 + j;
    }
  }  

  /* Partition in the 3rd direction taking into account the 1st and 2nd direction */
  if(vpartitions3 > 1) {
    if(info) printf("Ordering in the 3rd direction.\n");

    for(j=1;j<=noelements;j++) {
      nonodes = data->elementtypes[j]%100;
      x = y = z = 0.0;
      for(i=0;i<nonodes;i++) {
	k = data->topology[j][i];
	x += data->x[k];
	y += data->y[k];
	if(data->dim==3) z += data->z[k];
      }
      arrange[j] = (-cz*x - cy*y + cx*z) / nonodes;
    }
    SortIndex(noelements,arrange,indx);

    for(i=1;i<=vpartitions;i++)
      nopart[i] = 0;
    
    elemsinpart = noelements / (vpartitions1*vpartitions2*vpartitions3);
    for(i=1;i<=noelements;i++) {
      j = 0;
      ind = indx[i];
      do {
	j++;
	k = (inpart[ind]-1)*vpartitions3 + j;
      }
      while(nopart[k] >= elemsinpart && j < vpartitions3);
    
      nopart[k] += 1;
      inpart[ind] = (inpart[ind]-1)*vpartitions3 + j;
    }
  }


  /* For periodic systems the number of virtual partitions is larger. Now map the mesh so that the 
     1st and last partition for each direction will be joined */
  if(periodic) {
    int *partmap;
    int p1,p2,p3,q1,q2,q3;
    int P,Q;
    p1=p2=p3=1;
    partmap = Ivector(1,vpartitions);
    for(i=1;i<=vpartitions;i++)
      partmap[i] = 0;
    for(p1=1;p1<=vpartitions1;p1++) {
      q1 = p1;
      if(dimper[0] && vpartitions1 > 1) {
	if(q1==vpartitions1) q1 = 0;
	q1 = q1/2 + 1;
      }
      for(p2=1;p2<=vpartitions2;p2++) {
	q2 = p2;
	if(dimper[1] && vpartitions2 > 1) {
	  if(q2==vpartitions2) q2 = 0;
	  q2 = q2/2 + 1;
	}
	for(p3=1;p3<=vpartitions3;p3++) {
	  q3 = p3;
	  if(dimper[2] && vpartitions3 > 1) {
	    if(q3==vpartitions3) q3 = 0;
	    q3 = q3/2 + 1;
	  }
	  
	  P = vpartitions3 * vpartitions2 * (p1 - 1) + vpartitions3 * (p2-1) + p3;
	  Q = partitions3 * partitions2 * (q1 - 1) + partitions3 * (q2-1) + q3;

	  partmap[P] = Q;
	}
      }
    }
    for(i=1;i<=noelements;i++)
      inpart[i] = partmap[inpart[i]];
    free_Ivector(partmap,1,vpartitions);
  }

  for(i=1;i<=partitions;i++)
    nopart[i] = 0;
  for(i=1;i<=noelements;i++) 
    nopart[inpart[i]] += 1;

  minpart = maxpart = nopart[1];
  for(i=1;i<=partitions;i++) {
    minpart = MIN( nopart[i], minpart );
    maxpart = MAX( nopart[i], maxpart );
  }

  free_Rvector(arrange,1,noelements);
  free_Ivector(nopart,1,vpartitions);
  free_Ivector(indx,1,noelements);

  PartitionNodesByElements(data,info);

  if(info) printf("Succesfully made a partitioning with %d to %d elements.\n",minpart,maxpart);

  return(0);
}



int PartitionSimpleNodes(struct FemType *data,int dimpart[],int dimper[],
			 int partorder, Real corder[],int info)
{
  int i,j,k,k0,l,ind,minpart,maxpart;
  int noknots, noelements,nonodes,elemsinpart,periodic;
  int partitions1,partitions2,partitions3,partitions;
  int vpartitions1,vpartitions2,vpartitions3,vpartitions,hit;
  int *indx,*part1,*nopart,*inpart,*nodepart;
  Real *arrange;
  Real x,y,z,cx,cy,cz;
  
  partitions1 = dimpart[0];
  partitions2 = dimpart[1];
  partitions3 = dimpart[2];
  if(data->dim < 3) partitions3 = 1;
  partitions = partitions1 * partitions2 * partitions3;

  if(partitions1 < 2 && partitions2 < 2 && partitions3 < 2) {
    printf("Some of the divisions must be larger than one: %d %d %d\n",
	   partitions1, partitions2, partitions3 );
    bigerror("Partitioning not performed");
  }

  if(partitions >= data->noelements) {
    printf("There must be fever partitions than elements (%d vs %d)!\n",
	   partitions,data->noelements);
    bigerror("Partitioning not performed");
  }
    
  if(!data->partitionexist) {
    data->partitionexist = TRUE;
    data->elempart = Ivector(1,data->noelements);
    data->nodepart = Ivector(1,data->noknots);
    data->nopartitions = partitions;
  }
  inpart = data->elempart;
  nodepart = data->nodepart;

  vpartitions1 = partitions1;
  vpartitions2 = partitions2;
  vpartitions3 = partitions3;
  periodic = dimper[0] || dimper[1] || dimper[2];
  if(periodic) {
    if(dimper[0] && partitions1 > 1) vpartitions1 *= 2;
    if(dimper[1] && partitions2 > 1) vpartitions2 *= 2;
    if(dimper[2] && partitions3 > 1) vpartitions3 *= 2;
  }
  vpartitions = vpartitions1 * vpartitions2 * vpartitions3;

  nopart = Ivector(1,vpartitions);
  noelements = data->noelements;
  noknots = data->noknots;

  if(info) printf("Making a simple partitioning for %d nodes in %d-dimensions.\n",
		  noknots,data->dim);

  arrange = Rvector(1,noknots);
  indx = Ivector(1,noknots);

  if(partorder) {
    cx = corder[0];
    cy = corder[1];
    cz = corder[2];    
  }
  else {
    cx = 1.0;
    cy = 0.0001;
    cz = cy*cy;
  }

  z = 0.0;

  for(i=1;i<=noknots;i++) 
    nodepart[i] = 1;  

  if(vpartitions1 > 1) {
    if(info) printf("Ordering 1st direction with (%.3lg*x + %.3lg*y + %.3lg*z)\n",cx,cy,cz);
    for(j=1;j<=noknots;j++) {
      x = data->x[j];
      y = data->y[j];
      if(data->dim==3) z = data->z[j];
      arrange[j] = cx*x + cy*y + cz*z;
    }
    SortIndex(noknots,arrange,indx);

    for(i=1;i<=noknots;i++) {
      ind = indx[i];
      k = (i*vpartitions1-1)/noknots+1;
      nodepart[ind] = k;
    }
  } 

  /* Partition in the 2nd direction taking into account the 1st direction */
  if(vpartitions2 > 1) {
    if(info) printf("Ordering in the 2nd direction.\n");
    for(j=1;j<=noknots;j++) {
      x = data->x[j];
      y = data->y[j];
      if(data->dim==3) z = data->z[j];
      arrange[j] = -cy*x + cx*y + cz*z;
    }
    SortIndex(noknots,arrange,indx);
    
    for(i=1;i<=vpartitions;i++)
      nopart[i] = 0;
    
    elemsinpart = noknots / (vpartitions1*vpartitions2);
    j = 1;
    for(i=1;i<=noknots;i++) {
      ind = indx[i];
      k0 = (nodepart[ind]-1) * vpartitions2;
      for(l=1;l<=vpartitions2;l++) {
	hit = FALSE;
	if( j < vpartitions ) {
	  if( nopart[k0+j] >= elemsinpart ) {
	    j += 1;
	    hit = TRUE;
	  }
	}
	if( j > 1 ) {
	  if( nopart[k0+j-1] < elemsinpart ) {
	    j -= 1;
	    hit = TRUE;
	  }
	}
	if( !hit ) break;
      }
      k = k0 + j;
      nopart[k] += 1;
      nodepart[ind] = k;
    }
  }  

  /* Partition in the 3rd direction taking into account the 1st and 2nd direction */
  if(vpartitions3 > 1) {
    if(info) printf("Ordering in the 3rd direction.\n");
    for(j=1;j<=noknots;j++) {
      x = data->x[j];
      y = data->y[j];
      if(data->dim==3) z = data->z[j];
      arrange[j] = -cz*x - cy*y + cx*z;
    }
    SortIndex(noknots,arrange,indx);

    for(i=1;i<=vpartitions;i++)
      nopart[i] = 0;
    
    elemsinpart = noknots / (vpartitions1*vpartitions2*vpartitions3);
    j = 1;
    for(i=1;i<=noknots;i++) {
      ind = indx[i];
      k0 = (nodepart[ind]-1)*vpartitions3;

      for(l=1;l<=vpartitions;l++) {
	hit = FALSE;
	if( j < vpartitions3 ) {
	  if( nopart[k0+j] >= elemsinpart ) {
	    j += 1;
	    hit = TRUE;
	  }
	}
	if( j > 1 ) {
	  if( nopart[k0+j-1] < elemsinpart ) {
	    j -= 1;
	    hit = TRUE;
	  }
	}
	if( !hit ) break;
      }
      k = k0 + j;
      nopart[k] += 1;
      nodepart[ind] = k;
    }
  }
  

  /* For periodic systems the number of virtual partitions is larger. Now map the mesh so that the 
     1st and last partition for each direction will be joined */
  if(periodic) {
    int *partmap;
    int p1,p2,p3,q1,q2,q3;
    int P,Q;
    p1=p2=p3=1;
    partmap = Ivector(1,vpartitions);
    for(i=1;i<=vpartitions;i++)
      partmap[i] = 0;
    for(p1=1;p1<=vpartitions1;p1++) {
      q1 = p1;
      if(dimper[0] && vpartitions1 > 1) {
	if(q1==vpartitions1) q1 = 0;
	q1 = q1/2 + 1;
      }
      for(p2=1;p2<=vpartitions2;p2++) {
	q2 = p2;
	if(dimper[1] && vpartitions2 > 1) {
	  if(q2==vpartitions2) q2 = 0;
	  q2 = q2/2 + 1;
	}
	for(p3=1;p3<=vpartitions3;p3++) {
	  q3 = p3;
	  if(dimper[2] && vpartitions3 > 1) {
	    if(q3==vpartitions3) q3 = 0;
	    q3 = q3/2 + 1;
	  }
	  
	  P = vpartitions3 * vpartitions2 * (p1 - 1) + vpartitions3 * (p2-1) + p3;
	  Q = partitions3 * partitions2 * (q1 - 1) + partitions3 * (q2-1) + q3;

	  partmap[P] = Q;
	}
      }
    }
    for(i=1;i<=noknots;i++)
      nodepart[i] = partmap[nodepart[i]];
    free_Ivector(partmap,1,vpartitions);
  }


  for(i=1;i<=partitions;i++)
    nopart[i] = 0;
  for(i=1;i<=noknots;i++) 
    nopart[nodepart[i]] += 1;
  
  minpart = maxpart = nopart[1];
  for(i=1;i<=partitions;i++) {
    minpart = MIN( nopart[i], minpart );
    maxpart = MAX( nopart[i], maxpart );
  }

  free_Rvector(arrange,1,noelements);
  free_Ivector(nopart,1,partitions);
  free_Ivector(indx,1,noelements);

  PartitionElementsByNodes(data,info);

  if(info) printf("Succesfully made a partitioning with %d to %d nodes.\n",minpart,maxpart);

  return(0);
}


#if PARTMETIS 
int PartitionMetisElements(struct FemType *data,int partitions,int dual,int info)
{
  int i,j,periodic, highorder, noelements, noknots, ne, nn, sides;
  int nodesd2, etype, numflag, nparts, edgecut;
  int *neededby,*metistopo;
  int *indxper,*inpart,*epart,*npart;

  if(info) printf("Making a Metis partitioning for %d elements in %d-dimensions.\n",
		  data->noelements,data->dim);

  if(!data->partitionexist) {
    data->partitionexist = TRUE;
    data->elempart = Ivector(1,data->noelements);
    data->nodepart = Ivector(1,data->noknots);
    data->nopartitions = partitions;
  }
  inpart = data->elempart;

  /* Are there periodic boundaries. This information is used to join the boundaries. */
  periodic = data->periodicexist;
  if(periodic) {
    if(info) printf("There seems to be peridic boundaries\n");
    indxper = data->periodic;
  }

  highorder = FALSE;
  noelements = data->noelements;
  noknots = data->noknots;
  
  ne = noelements;
  nn = noknots;

  sides = data->elementtypes[1]/100;
  for(i=1;i<=noelements;i++) {
    if(sides != data->elementtypes[i]/100) {
      printf("Nodal Metis partition requires that all the elements are of the same type!\n");
      bigerror("Partitioning not performed");
    }
    if(sides == 3 && data->elementtypes[i]%100 > 3) highorder = TRUE;
    if(sides == 4 && data->elementtypes[i]%100 > 4) highorder = TRUE;
    if(sides == 5 && data->elementtypes[i]%100 > 4) highorder = TRUE;
    if(sides == 8 && data->elementtypes[i]%100 > 8) highorder = TRUE;
  }

  if(info && highorder) printf("There are at least some higher order elements\n");

  if(sides == 3) {
    if (info) printf("The mesh seems to consist of triangles\n");
    nodesd2 = 3;
    etype = 1;
  }
  else if(sides == 4)  {
    if(info) printf("The mesh seems to consist of quadrilaterals\n");
    nodesd2 = 4;
    etype = 4;
  }
  else if(sides == 5) {
    if(info) printf("The mesh seems to consist of tetrahedra\n");
    nodesd2 = 4;
    etype = 2;
  }
  else if(sides == 8) {
    if(info) printf("The mesh seems to consist of bricks\n");
    nodesd2 = 8;
    etype = 3;
  }
  else {
    printf("Nodal Metis partition only for triangles, quads, tets and bricks!\n");
    bigerror("Partitioning not performed");
  }

  neededby = Ivector(1,noknots);
  metistopo = Ivector(0,noelements*nodesd2-1);
  epart = Ivector(0,noelements-1);

  numflag = 0;
  nparts = partitions;
  
  for(i=1;i<=noknots;i++) 
    neededby[i] = 0;
  if(periodic) {
    for(i=1;i<=noelements;i++) 
      for(j=0;j<nodesd2;j++) 
	neededby[indxper[data->topology[i][j]]] = 1;
  }
  else {
    for(i=1;i<=noelements;i++) 
      for(j=0;j<nodesd2;j++) 
	neededby[data->topology[i][j]] = 1;
  }

  j = 0;
  for(i=1;i<=noknots;i++) 
    if(neededby[i]) 
      neededby[i] = ++j;
  nn = j;
  npart = Ivector(0,nn-1);
  
  if(periodic) {
    for(i=0;i<noelements;i++) 
      for(j=0;j<nodesd2;j++) 
	metistopo[nodesd2*i+j] = neededby[indxper[data->topology[i+1][j]]]-1;
  }    
  else {
    for(i=0;i<noelements;i++) 
      for(j=0;j<nodesd2;j++) 
	metistopo[nodesd2*i+j] = neededby[data->topology[i+1][j]]-1;    
  }

  if(info) printf("Using %d nodes of %d possible nodes in the Metis graph\n",nn,noknots);

  if(dual) {
    if(info) printf("Starting graph partitioning METIS_PartMeshDual.\n");  
    METIS_PartMeshDual(&ne,&nn,metistopo,&etype,
		       &numflag,&nparts,&edgecut,epart,npart);
  }
  else {
    if(info) printf("Starting graph partitioning METIS_PartMeshNodal.\n");  
    METIS_PartMeshNodal(&ne,&nn,metistopo,&etype,
			&numflag,&nparts,&edgecut,epart,npart);
  }

  /* Set the partition given by Metis for each element. */
  for(i=1;i<=noelements;i++) {
    inpart[i] = epart[i-1]+1;
    if(inpart[i] < 1 || inpart[i] > partitions) 
      printf("Invalid partition %d for element %d\n",inpart[i],i);
  }

  if( highorder ) {
    PartitionNodesByElements(data,info);
  }
  else {
    /* Set the partition given by Metis for each node. */
    for(i=1;i<=noknots;i++) {
      if(periodic) 
	j = neededby[indxper[i]];
      else
	j = neededby[i];
      if(!j) printf("Cant set partitioning for node %d\n",i);
      data->nodepart[i] = npart[j-1]+1;
      if(data->nodepart[i] < 1 || data->nodepart[i] > partitions) 
	printf("Invalid partition %d for node %d\n",data->nodepart[i],i);
    }
  }

  free_Ivector(neededby,1,noknots);
  free_Ivector(metistopo,0,noelements*nodesd2-1);
  free_Ivector(epart,0,noelements-1);
  free_Ivector(npart,0,nn-1);

  if(info) printf("Succesfully made a Metis partition using the element mesh.\n");

  return(0);
}



int PartitionMetisNodes(struct FemType *data,struct BoundaryType *bound,
			struct ElmergridType *eg,int partitions,int metisopt,int info)
{
  int i,j,k,l,noelements,noknots;
  int nn,con,maxcon,totcon,options[5];
  int *xadj,*adjncy,*vwgt,*adjwgt,wgtflag,*npart;
  int numflag,nparts,edgecut;
  int *indxper;

  if(info) printf("Making a Metis partitioning for %d nodes in %d-dimensions.\n",
		  data->noknots,data->dim);

  CreateDualGraph(data,TRUE,info);

  noknots = data->noknots;
  noelements = data->noelements;
  maxcon = data->dualmaxconnections;

  totcon = 0;
  for(i=1;i<=noknots;i++) {
    for(j=0;j<maxcon;j++) {
      con = data->dualgraph[j][i];
      if(con) totcon++;
    }
  }

  if(0) printf("There are %d connections alltogether.\n",totcon);

  xadj = Ivector(0,noknots);
  adjncy = Ivector(0,totcon-1);
  for(i=0;i<totcon;i++) 
    adjncy[i] = 0;

  totcon = 0;
  for(i=1;i<=noknots;i++) {
    xadj[i-1] = totcon;
    for(j=0;j<maxcon;j++) {
      con = data->dualgraph[j][i];
      if(con) {
	adjncy[totcon] = con-1;
	totcon++;
      }
    }
  }
  xadj[noknots] = totcon;


  nn = noknots;
  numflag = 0;
  nparts = partitions;
  npart = Ivector(0,noknots-1);
  wgtflag = 0;
  for(i=0;i<5;i++) options[i] = 0;
  options[0] = 0;
  options[1] = 3;
  options[2] = 1;
  options[3] = 3;

  vwgt = NULL;
  adjwgt = NULL;

  /* Make the periodic connections the strongest ones */
  if(data->periodicexist) {
    if(info) printf("Setting periodic connections to dominate %d\n",totcon);
    adjwgt = Ivector(0,totcon-1);
    for(i=0;i<totcon;i++)
      adjwgt[i] = 1;
    for(i=0;i<noknots;i++) {
      j = data->periodic[i+1]-1;
      if(j == i) continue;
      for(k=xadj[i];k<xadj[i+1];k++) 
	if(adjncy[k] == j) adjwgt[k] = maxcon;
    }
    if(data->periodicexist && metisopt != 3) {
      printf("For periodic BCs Metis subroutine METIS_PartGraphKway is enforced\n");
      metisopt = 3;
    }
    wgtflag = 1;
  }
  
  /* Add weight if there is a constraint */
  if(eg->connect) {
    int maxweight;
    int con,bc,bctype,sideelemtype,sidenodes;
    int j2,ind,ind2;
    int sideind[MAXNODESD1];

    maxweight = noknots+noelements;

    printf("Adding weight of %d\n",maxweight);

    adjwgt = Ivector(0,totcon-1);
    for(i=0;i<totcon;i++)
      adjwgt[i] = 1;
    wgtflag = 1;

    for(con=1;con<=eg->connect;con++) {
      bctype = eg->connectbounds[con-1];

      for(bc=0;bc<MAXBOUNDARIES;bc++) {    
	if(bound[bc].created == FALSE) continue;
	if(bound[bc].nosides == 0) continue;
	
	for(i=1;i<=bound[bc].nosides;i++) {
	  if(bound[bc].types[i] != bctype) continue;
	  
	  GetElementSide(bound[bc].parent[i],bound[bc].side[i],bound[bc].normal[i],
			 data,sideind,&sideelemtype);
	  sidenodes = sideelemtype%100;
      
	  for(j=0;j<sidenodes;j++) {
	    for(j2=0;j2<sidenodes;j2++) {
	      if(j==j2) continue;

	      ind = sideind[j]-1;
	      ind2 = sideind[j2]-1;

	      for(k=xadj[ind];k<xadj[ind+1];k++) 
		if(adjncy[k] == ind2) adjwgt[k] = maxweight;
	    }
	  }
	}
      }
    }
  }

  if(metisopt == 2) {
    if(info) printf("Starting graph partitioning METIS_PartGraphRecursive.\n");  
    METIS_PartGraphRecursive(&nn,xadj,adjncy,vwgt,adjwgt,&wgtflag,
			     &numflag,&nparts,&options[0],&edgecut,npart);
  }
  else if(metisopt == 3) {
    if(info) printf("Starting graph partitioning METIS_PartGraphKway.\n");      
    METIS_PartGraphKway(&nn,xadj,adjncy,vwgt,adjwgt,&wgtflag,
			&numflag,&nparts,&options[0],&edgecut,npart);
  }
  else if(metisopt == 4) {
    if(info) printf("Starting graph partitioning METIS_PartGraphVKway.\n");      
    METIS_PartGraphVKway(&nn,xadj,adjncy,vwgt,adjwgt,&wgtflag,
			&numflag,&nparts,&options[0],&edgecut,npart);
  }
  else 
    printf("Unknown Metis option %d\n",metisopt);

  if(info) printf("Finished Metis graph partitioning call.\n");


  free_Ivector(adjncy,0,totcon-1);
  if(wgtflag == 1)  free_Ivector(adjwgt,0,totcon-1);

  if(!data->partitionexist) {
    data->partitionexist = TRUE;
    data->elempart = Ivector(1,data->noelements);
    data->nodepart = xadj; /* Dirty reuse to save little memory and time */
    data->nopartitions = partitions;
  }

  /* Set the partition given by Metis for each node. */
  for(i=1;i<=noknots;i++) 
    data->nodepart[i] = npart[i-1]+1;

  if(eg->connect) {
    int con,bc,bctype,sideelemtype,sidenodes,par;
    int ind,sideind[MAXNODESD1];
    int *sidehits,sidepartitions;

    printf("Checking connection integrity\n");
    sidehits = Ivector(1,partitions);

    for(con=1;con<=eg->connect;con++) {
      bctype = eg->connectbounds[con-1];

      for(i=1;i<=partitions;i++)
	sidehits[i] = 0;

      for(bc=0;bc<MAXBOUNDARIES;bc++) {    
	if(bound[bc].created == FALSE) continue;
	if(bound[bc].nosides == 0) continue;
	
	for(i=1;i<=bound[bc].nosides;i++) {
	  if(bound[bc].types[i] != bctype) continue;
	  
	  GetElementSide(bound[bc].parent[i],bound[bc].side[i],bound[bc].normal[i],
			 data,sideind,&sideelemtype);
	  sidenodes = sideelemtype%100;
      
	  for(j=0;j<sidenodes;j++) {
	    ind = sideind[j];
	    par = data->nodepart[ind];
	    sidehits[par] += 1;
	  }
	}   
      }

      sidepartitions = 0;
      for(i=1;i<=partitions;i++)
	if( sidehits[i] ) sidepartitions += 1;

      if(sidepartitions != 1) {
	printf("PartitionMetisNodes: side %d belongs to %d partitions\n",bctype,sidepartitions);
	if(1) 
	  bigerror("Parallel constraints may not be set!");
	else
	  printf("**************** Warning *******************\n");
      }
    }
  }


  PartitionElementsByNodes(data,info);

  free_Ivector(npart,0,noknots-1);

  if(info) printf("Succesfully made a Metis partition using the dual graph.\n");

  return(0);
}
#endif  


static void CheckPartitioning(struct FemType *data,int info)
{
  int i,j,k,partitions,part,part2,noknots,noelements,mini,maxi,sumi,hit,ind,nodesd2,elemtype;
  int *elempart, *nodepart,*elemsinpart,*nodesinpart,*sharedinpart;

  noknots = data->noknots;
  noelements = data->noelements;
  partitions = data->nopartitions;
  elemsinpart = Ivector(1,partitions);
  nodesinpart = Ivector(1,partitions);
  sharedinpart = Ivector(1,partitions);
  for(i=1;i<=partitions;i++)
    elemsinpart[i] = nodesinpart[i] = sharedinpart[i] = 0;

  if(info) printf("Checking for partitioning\n");

  /* Check that division of elements */
  elempart = data->elempart;
  j=0;
  for(i=1;i<=data->noelements;i++) {
    part = elempart[i];
    if(part < 1 || part > partitions) 
      j++;
    else 
      elemsinpart[part] += 1;
  }      
  if(j) {
    printf("Bad initial partitioning: %d elements do not belong anywhere!\n",j);
    bigerror("Can't continue with broken partitioning");
  }    

  /* Check the division of nodes */
  nodepart = data->nodepart; 
  j=0;
  for(i=1;i<=data->noknots;i++) {
    part = nodepart[i];
    if(part < 1 || part > partitions) 
      j++;
    else 
      nodesinpart[part] += 1;
  }
  
  if(j) {
    printf("Bad initial partitioning: %d nodes do not belong anywhere!\n",j);
    bigerror("Can't continue with broken partitioning");
  }

  if(data->partitiontableexists) {
    for(i=1;i<=noknots;i++) {
      part = nodepart[i];
      for(j=1;j<=data->maxpartitiontable;j++) {
	part2 = data->partitiontable[j][i];
	if(!part2) break;
	if(part != part2) sharedinpart[part2] += 1;
      }
    }
  }

  if(info) {
    printf("Information on partition bandwidth\n");
    if(partitions <= 4) {
      printf("Distribution of elements, nodes and shared nodes\n");
      printf("     %-10s %-10s %-10s %-10s\n","partition","elements","nodes","shared");
      for(i=1;i<=partitions;i++)
	printf("     %-10d %-10d %-10d %-10d\n",i,elemsinpart[i],nodesinpart[i],sharedinpart[i]);
    } 
    else {
      mini = maxi = elemsinpart[1];
      for(i=1;i<=partitions;i++) {
	mini = MIN( elemsinpart[i], mini);
	maxi = MAX( elemsinpart[i], maxi);
      }
      printf("Average %d elements with range %d in partition\n",noelements/partitions,maxi-mini);

      mini = maxi = nodesinpart[1];
      for(i=1;i<=partitions;i++) {
	mini = MIN( nodesinpart[i], mini);
	maxi = MAX( nodesinpart[i], maxi);
      }
      printf("Average %d nodes with range %d in partition\n",noknots/partitions,maxi-mini);

      sumi = 0;
      mini = maxi = sharedinpart[1];
      for(i=1;i<=partitions;i++) {
	mini = MIN( sharedinpart[i], mini);
	maxi = MAX( sharedinpart[i], maxi);
	sumi += sharedinpart[i];
      }
      printf("Average %d shared nodes with range %d in partition\n",sumi/partitions,maxi-mini);
    }
  }

  if(!data->maxpartitiontable) return;

  if(0) printf("Checking that each node in elements belongs to nodes\n");
  for(i=1;i<=data->noelements;i++) {
    part = elempart[i];
    elemtype = data->elementtypes[i];
    nodesd2 = elemtype % 100;

    for(j=0;j < nodesd2;j++) {
      ind = data->topology[i][j];
      
      hit = FALSE;
      for(k=1;k<=data->maxpartitiontable;k++) {
	part2 = data->partitiontable[k][ind];
	if( part == part2 ) hit = TRUE;
	if(hit && !part) break;
      }
      if(!hit) {
	printf("******** Warning *******\n");
	printf("Node %d in element %d does not belong to partition %d (%d)\n",ind,i,part,part2);
	printf("elemtype = %d nodesd2 = %d\n",elemtype,nodesd2);
	for(k=0;k < nodesd2;k++) 
	  printf("ind[%d] = %d\n",k,data->topology[i][k]);
      }
    }
  }

  if(0) printf("Checking that each node in partition is shown in partition list\n");
  for(i=1;i<=data->noknots;i++) {
    part = nodepart[i];
    
    hit = FALSE;
    for(j=1;j<=data->maxpartitiontable;j++) {
      part2 = data->partitiontable[j][i];
      if( part == part2 ) hit = TRUE;
	if(hit && !part) break;
    }
    if(!hit) {
      printf("***** Node %d in partition %d is not in partition list\n",i,part);
    }
  }
}


static int OptimizePartitioningAtBoundary(struct FemType *data,struct BoundaryType *bound,int info)
{
  int i,j,k,l,n,m,boundaryelems,ind,periodic,hit,hit2;
  int dompart,part1,part2,newmam,mam1,mam2,part,discont,nodesd2;
  int *alteredparent;


  if(!data->partitionexist) {
    printf("OptimizePartitioningAtDap: this should be called only after partitioning\n");
    bigerror("Optimization not performed!");
  }

  if(1) printf("Optimizing the partitioning at boundaries.\n");

  /* Memorize the original parent */
  alteredparent = Ivector(1,data->noelements);
  for(i=1;i<=data->noelements;i++)
    alteredparent[i] = data->elempart[i];

  
  /* Set the secondary parent to be a parent also because we want all 
     internal BCs to be within the same partition. 
     Also set the nodes of the altered elements to be in the desired partition. */
  k = 0;
  do {
    k++;
    boundaryelems = 0;

    for(j=0;j < MAXBOUNDARIES;j++) {
      if(!bound[j].created) continue;
      for(i=1; i <= bound[j].nosides; i++) {
	if(bound[j].ediscont)
	  if(bound[j].discont[i]) continue;

	mam1 = bound[j].parent[i];
	mam2 = abs(bound[j].parent2[i]);
	if(!mam1 || !mam2) continue;
	part1 = data->elempart[mam1];
	part2 = data->elempart[mam2];
	if(part1 == part2) continue;

	  
	/* The first iterations check which parents is ruling 
	   thereafter choose pragmatically the other to overcome
	   oscillating solutions. */
	if(k < 5) {
	  hit = hit2 = 0;
	  nodesd2 = data->elementtypes[mam1] % 100;
	  for(l=0;l < nodesd2;l++) {
	    ind = data->topology[mam1][l];
	    if(data->nodepart[ind] == part1) hit++;
	    if(data->nodepart[ind] == part2) hit2++;
	  }
	  nodesd2 = data->elementtypes[mam2] % 100;    
	  for(l=0;l < nodesd2;l++) {
	    ind = data->topology[mam2][l];
	    if(data->nodepart[ind] == part1) hit++;
	    if(data->nodepart[ind] == part2) hit2++;
	  }	  
	} 
	else {
	  hit2 = 0;
	  hit = 1;
	}   

	/* Make the more ruling parent dominate the whole boundary */
	if(hit > hit2) {
	  dompart = part1;
	  newmam = mam2;
	}
	else {
	  dompart = part2;
	  newmam = mam1;
	}
	
	data->elempart[newmam] = dompart;
	boundaryelems++;	    

	/* Move the ownership of all nodes to the leading partition */
	if(0) {
	  nodesd2 =  data->elementtypes[newmam] % 100;
	  for(l=0;l < nodesd2;l++) {
	    ind = data->topology[newmam][l];
	    data->nodepart[ind] = dompart;
	  }
	}
	else if(0) {
	  nodesd2 = data->elementtypes[mam1] % 100;
	  for(l=0;l < nodesd2;l++) {
	    ind = data->topology[mam1][l];
	    data->nodepart[ind] = dompart;
	  }	    
	  nodesd2 = data->elementtypes[mam2] % 100;
	  for(l=0;l < nodesd2;l++) {
	    ind = data->topology[mam2][l];
	    data->nodepart[ind] = dompart;
	  }	    
	}

      }
    }
    if(info && boundaryelems) 
      printf("%d bulk elements with BCs removed from interface.\n",boundaryelems);
  } while(boundaryelems && k < 10);


  j = 0;
  for(i=1;i<=data->noelements;i++) {
    if(alteredparent[i] == data->elempart[i]) 
      alteredparent[i] = 0;
    else 
      j++;
  }
  free_Ivector(alteredparent,1,data->noelements);
  if(info) printf("Ownership of %d parents was changed at BCs\n",j); 



  /* Remove the negative secondary parents that were only used to optimize the partitioning */ 
  for(j=0;j < MAXBOUNDARIES;j++) {
    if(!bound[j].created) continue;
    for(i=1; i <= bound[j].nosides; i++) 
      bound[j].parent2[i] = MAX(0, bound[j].parent2[i]);
  }

  if(0) printf("The partitioning at discontinous gaps was optimized.\n"); 
  return(0);
}


static void Levelize(int n,int level,int *maxlevel,int *levels,int *rows,int *cols,int *done)
{    
   int j,k;

   levels[n] = level;
   done[n] = TRUE;
   *maxlevel = MAX( *maxlevel,level );

   for( j=rows[n]; j<rows[n+1]; j++ ) {
      k = cols[j];
      if ( !done[k] ) Levelize(k,level+1,maxlevel,levels,rows,cols,done);
   }
}


static int RenumberCuthillMckee( int nrows, int *rows, int *cols, int *iperm )
{
  int i,j,k,n,startn,mindegree,maxlevel,newroot,bw_bef,bw_aft;
  int *level,*degree,*done;

  done   = Ivector(0,nrows-1);
  level  = Ivector(0,nrows-1);
  degree = Ivector(0,nrows-1);

  bw_bef = 0;
  for(i=0; i<nrows; i++ )
  {
    for( j=rows[i]; j<rows[i+1]; j++ )
      bw_bef = MAX( bw_bef, ABS(cols[j]-i)+1 );
    degree[i] = rows[i+1]-rows[i];
  }
  printf( "RCM: Bandwidth before: %d\n", bw_bef );

   startn = 0;
   mindegree = degree[startn];
   for( i=0; i<nrows; i++ ) {
     if ( degree[i] < mindegree ) {
       startn = i;
       mindegree = degree[i];
     }
     level[i] = 0;
   }

   maxlevel = 0;
   for( i=0; i<nrows; i++ ) done[i]=FALSE;

   Levelize( startn,0,&maxlevel,level,rows,cols,done );

   newroot = TRUE;
   while(newroot) {
     newroot = FALSE;
     mindegree = degree[startn];
     k = startn;

     for( i=0; i<nrows; i++ ) {
       if ( level[i] == maxlevel ) {
         if ( degree[i] < mindegree ) {
           k = i;
           mindegree = degree[i];
         }
       }
     }

     if ( k != startn ) {
       j = maxlevel;
       maxlevel = 0;
       for(i=0; i<nrows; i++ ) done[i]=FALSE;

       Levelize( k,0,&maxlevel,level,rows,cols,done );

       if ( j > maxlevel ) {
         newroot = TRUE;
         startn = j;
       }
     }
   }

  for(i=0; i<nrows; i++ ) done[i]=-1,iperm[i]=-1;

  done[0]=startn;
  iperm[startn]=0;
  i=1;

  for( j=0; j<nrows; j++ ) {
    if ( done[j]<0 ) {
      for( k=0; k<nrows; k++ ) {
         if ( iperm[k]<0 ) {
             done[i]=k;
             iperm[k]=i;
             i++;
             break;
           }
         }
       }

    for( k=rows[done[j]]; k<rows[done[j]+1]; k++) { 
        n = cols[k];
       if ( iperm[n]<0 ) {
         done[i] = n;
         iperm[n] = i;
         i++;
        }
      }
    }

    for( i=0; i<nrows; i++ )
      iperm[done[i]] = nrows-1-i;

    bw_aft = 0;
    for(i=0; i<nrows; i++ )
      for( j=rows[i]; j<rows[i+1]; j++ )
        bw_aft = MAX( bw_aft, ABS(iperm[cols[j]]-iperm[i])+1 );

 printf( "RCM: Bandwidth after: %d\n", bw_aft );

   free_Ivector(level,0,nrows-1);
   free_Ivector(done,0,nrows-1);
   free_Ivector(degree,0,nrows-1);

   return bw_aft < bw_bef;
}



static void RenumberPartitions(struct FemType *data,int info)
{
  int i,j,k,l,n,m,hit,con,totcon,noelements,noknots,partitions;
  int maxneededtimes,totneededtimes;
  int part,part1,part2,bw_reduced;
  int *nodepart,*elempart;
  int *perm;
  int *xadj,*adjncy;
  int *partparttable[MAXCONNECTIONS];


  if(info) printf("Renumbering partitions to minimize bandwidth.\n");  

  partitions = data->nopartitions;
  noelements = data->noelements;
  noknots = data->noknots;
  maxneededtimes = data->maxpartitiontable;


  /* Make the partition-partition list from the node-partition list */
  totneededtimes = 0;
  totcon = 0;
  for(i=1;i<=noknots;i++) {
    if(data->partitiontable[2][i] == 0) continue;
    for(j=1;j<=maxneededtimes;j++) {
      part1 =  data->partitiontable[j][i];
      if(!part1) break;

      for(k=1;k<=maxneededtimes;k++) {
	if(k==j) continue;
	part2 = data->partitiontable[k][i];
	if(!part2) break;

	hit = 0;
	for(l=1;l<=totneededtimes;l++) { 
	  if(partparttable[l][part1] == part2) {
	    hit = -1;
	    break;
	  }
	  else if(partparttable[l][part1] == 0) {
	    totcon++;
	    partparttable[l][part1] = part2;
	    hit = 1;
	    break;
	  }
	}
	if(!hit) {
	  totneededtimes++;
	  partparttable[totneededtimes] = Ivector(1,partitions);
	  for(m=1;m<=partitions;m++)
	    partparttable[totneededtimes][m] = 0;
	  partparttable[totneededtimes][part1] = part2;
	  totcon++;
	}
      }
    }
  }

  if(info) {
    printf("There are %d connections alltogether\n",totcon);
    printf("There are %.3lf connnections between partitions in average\n",1.0*totcon/partitions);
  }


  xadj = Ivector(0,partitions);
  adjncy = Ivector(0,totcon-1);
  for(i=0;i<totcon;i++) 
    adjncy[i] = 0;

  totcon = 0;
  for(i=1;i<=partitions;i++) {
    xadj[i-1] = totcon;
    for(j=1;j<=totneededtimes;j++) {
      con = partparttable[j][i];
      if(!con) continue;
      adjncy[totcon] = con-1;
      totcon++;
    }
  }    
  xadj[partitions] = totcon;

  perm = Ivector(0,partitions-1);
  bw_reduced = RenumberCuthillMckee( partitions, xadj, adjncy, perm );


  /* Print the new order of partitions */
  if(0 && info) {
    printf( "Partition order afer Cuthill-McKee bandwidth optimization: \n" );
    for(i=0;i<partitions;i++)
      printf("old=%d new=%d\n",i,perm[i] );
  }

  /* Use the renumbering or not */
  if(bw_reduced) {
    if(info) printf("Successful ordering: moving partitions to new positions\n");
    nodepart = data->nodepart;
    elempart = data->elempart;
    for(i=1;i<=noelements;i++) 
      elempart[i] = perm[elempart[i]-1]+1;
    for(i=1;i<=noknots;i++)
      nodepart[i] = perm[nodepart[i]-1]+1;
    for(i=1;i<=noknots;i++) {
      for(j=1;j<=maxneededtimes;j++) {
	part = data->partitiontable[j][i];
	if(!part) break;
	data->partitiontable[j][i] = perm[part-1]+1;
      }
    }
  }

  for(i=1;i<=totneededtimes;i++)
    free_Ivector(partparttable[i],1,partitions);
  free_Ivector(xadj,0,partitions);
  free_Ivector(adjncy,0,totcon-1);


}


static int CheckSharedDeviation(int *neededvector,int partitions,int info)
{
  int i,minshared,maxshared,dshared;
  Real sumshared, sumshared2, varshared;
    
  sumshared = sumshared2 = 0.0;
  minshared = maxshared = neededvector[1];
  for(i=1;i<=partitions;i++) {
    sumshared += neededvector[i];
    sumshared2 += neededvector[i] * neededvector[i];
    maxshared = MAX(maxshared, neededvector[i]);
    minshared = MIN(minshared, neededvector[i]);
  }
  
  dshared = maxshared - minshared;
  varshared = sqrt( 1.0*sumshared2 / partitions - 1.0*(sumshared/partitions)*(sumshared / partitions) );
  
  if(info) {
    printf("Maximum deviation in ownership %d\n",dshared);
    printf("Average deviation in ownership %.2lf\n",varshared);      
  }

  return(dshared);
}  




int OptimizePartitioning(struct FemType *data,struct BoundaryType *bound,int noopt,
			 int partbw, int info)
{
  int i,j,k,l,n,m,boundaryelems,noelements,partitions,ind,periodic,hit,hit2;
  int dompart,part1,part2,newmam,mam1,mam2,noknots,part,dshared,dshared0,avedshared;
  int *elempart,*nodepart,*neededtimes,*indxper,sharings;
  int nodesd2,maxneededtimes,*probnodes,optimize,target;
  int *neededvector;
  Real *rpart;

  if(!data->partitionexist) {
    printf("OptimizePartitioning: this should be called only after partitioning\n");
    bigerror("Optimization not performed!");
  }

  noknots = data->noknots;
  noelements = data->noelements;
  partitions = data->nopartitions;
  elempart = data->elempart;
  nodepart = data->nodepart; 
  periodic = data->periodicexist;
  if(periodic) indxper = data->periodic;

  /* This is the only routine that affects the ownership of elements */
  OptimizePartitioningAtBoundary(data,bound,info);

  /* Create a table showing to which partitions nodes belong to */
  CreatePartitionTable(data,info);
  maxneededtimes = data->maxpartitiontable;

  /* Renumber the bandwith of partition-partition connections */
  if(partbw) RenumberPartitions(data,info);

  /* Check partitioning after table is created for the first time */
  printf("Checking partitioning before optimization\n");
  CheckPartitioning(data,info);

  /* Calculate how many nodes is owned by each partition */
  neededvector = Ivector(1,partitions);  
  for(i=1;i<=partitions;i++) 
    neededvector[i] = 0;    
  for(i=1;i<=noknots;i++) 
    neededvector[nodepart[i]] += 1;
   
  if(!noopt) {
    optimize = 1;
    probnodes = Ivector(1,noknots);
    for(i=1;i<=noknots;i++)
      probnodes[i] = 0;
    printf("Applying aggressive optimization for load balancing\n");
  }

 optimizeownership:

  dshared = CheckSharedDeviation(neededvector,partitions,info);

  if(!noopt) {    
    
    /* Distribute the shared nodes as evenly as possible. */


    int target, same, nochanges, maxrounds, dtarget;

    target = noknots / partitions;
    maxrounds = 5;
    dtarget = 3;
    nochanges = 0;


    for(n=1;n<=maxrounds;n++) {
      nochanges = 0;

      for(i=1;i<=noknots;i++) {      
	ind = i;

	/* owner partition may only be changed it there a are two of them */
	k = data->partitiontable[2][ind];
	if(!k) continue;
	
	/* only apply the switch to cases with exactly two partitions 
	   to avoid the nasty multiply coupled nodes. */
	if(maxneededtimes > 2) 
	  if(data->partitiontable[3][ind]) continue;
	
	/* Do not change the ownership of nodes that cause topological problems */
	if(probnodes[ind]) continue;	  
	
	j = data->partitiontable[1][ind];

	/* Switch the owner to the smaller owner group if possible */
	if(abs(neededvector[j] - neededvector[k]) < dtarget ) continue;

	if(neededvector[j] < neededvector[k] && nodepart[ind] == k) {
	  nochanges++;
	  neededvector[j] += 1;
	  neededvector[k] -= 1;
	  nodepart[ind] = j;
	}
	else if(neededvector[k] < neededvector[j] && nodepart[ind] == j) {
	  nochanges++;
	  neededvector[k] += 1;
	  neededvector[j] -= 1;
	  nodepart[ind] = k;
	}
      }
      if(info && nochanges) printf("Changed the ownership of %d nodes\n",nochanges);
      
      dshared0 = dshared;
      dshared = CheckSharedDeviation(neededvector,partitions,info);
      
      if(dshared >= dshared0) break;
    }
  }


 optimizesharing:
  
  if(info) printf("Checking for problematic sharings\n"); 
  m = 0;

  if(partitions > 2) {  
    do {
      
      int i1,i2,e1,e2,owners,ownpart;
      int *elemparts,*invelemparts;
      int **knows;
      
      m++;
      sharings = 0;
      e1 = e2 = 0;
      
      if(m == 1 && optimize == 1) {
	elemparts = Ivector(1,partitions);
	invelemparts = Ivector(1,100);
	knows = Imatrix(1,100,1,100);
      }
      
      for(j=1;j<=partitions;j++) 
	elemparts[j] = 0;
      
      for(j=1;j<=100;j++) 
	invelemparts[j] = 0;
      
      for(j=1;j<=100;j++) 
	for(k=1;k<=100;k++)
	  knows[j][k] = 0;
      
      for(i=1;i<=noelements;i++) {
	int ownpart;
	
	owners = 0;
	nodesd2 = data->elementtypes[i] % 100;
	ownpart = FALSE;
	
	/* Check the number of owners in an element */
	for(j=0;j < nodesd2;j++) {
	  ind = data->topology[i][j];
	  k = nodepart[ind];
	  
	  /* Mark if the element partition is one of the owners */
	  if( k == elempart[i]) ownpart = TRUE;
	  if(!elemparts[k]) {
	    owners++;
	    elemparts[k] = owners;
	    invelemparts[owners] = k;
	  }
	}
	
	/* One strange owner is still ok. */
	if(owners - ownpart <= 1) {
	  /* Nullify the elemparts vector */
	  for(j=1;j<=owners;j++) {
	    k = invelemparts[j];
	    elemparts[k] = 0;
	  }
	  continue;
	}
	
	/* Check which of the partitions are related by a common node */
	for(j=0;j < nodesd2;j++) {
	  ind = data->topology[i][j];
	  for(l=1;l<=maxneededtimes;l++) {
	    e1 = data->partitiontable[l][ind];
	    if(!e1) break;
	    e1 = elemparts[e1];
	    if(!e1) continue;
	    for(k=l+1;k<=maxneededtimes;k++) {
	      e2 = data->partitiontable[k][ind];
	      if(!e2) break;
	      e2 = elemparts[e2];
	      if(!e2) continue;
	      knows[e1][e2] = knows[e2][e1] = TRUE;
	    }
	  }
	}    
	
	/* Check if there are more complex relations:
	   i.e. two partitions are joined at an element but not at the same node. */
	hit = FALSE;
	for(j=1;j<=owners;j++)
	  for(k=j+1;k<=owners;k++) {
	    if(!knows[j][k]) {
	      hit += 1;
	      i1 = invelemparts[j];
	      i2 = invelemparts[k];
	    }
	  }
	/* Nullify the elemparts vector */
	for(j=1;j<=owners;j++) {
	  k = invelemparts[j];
	  elemparts[k] = 0;
	}
	
	/* Nullify the knows matrix */
	for(j=1;j <= owners;j++) 
	  for(k=1;k <= owners;k++) 
	    knows[j][k] = FALSE;
	
	if(hit) {
	  e1 = e2 = 0;
	  
	  if(info) {
	    if( hit + m > 2 ) printf("Partitions %d and %d in element %d (%d owners) oddly related %d times\n",
				     i1,i2,i,owners,hit);
	  }
	  
	  
	  /* Count the number of nodes with wrong parents */
	  for(j=0;j < nodesd2;j++) {
	    ind = data->topology[i][j];
	    for(l=1;l<=maxneededtimes;l++) {
	      k = data->partitiontable[l][ind];
	      if(k == 0) break;
	      if(k == i1) e1++;
	      if(k == i2) e2++;
	    }
	  }
	  
	  /* Change the owner of those with less sharings */
	  for(j=0;j < nodesd2;j++) {
	    ind = data->topology[i][j];
	    k = nodepart[ind];
	    if((k == i1 && e1 < e2) || (k == i2 && e1 >= e2)) {
	      if(!noopt) probnodes[ind] += 1;
	      nodepart[ind] = elempart[i];
	      neededvector[elempart[i]] += 1;
	      neededvector[k] -= 1;
	    }
	  }	
	  sharings++;
	}
      }
      
      if(info && sharings) printf("Changed the ownership of %d nodes\n",sharings);
      
    } while (sharings > 0 && m < 3);
  
    if(info) {
      if(sharings) 
	printf("%d problematic sharings may still exist\n",sharings);
      else 
	printf("There shouldn't be any problematic sharings, knock, knock...\n");
    }
  }

  /* This seems to work also iteratively */
  if(!noopt && m+n > 10 && optimize < 50) {
    optimize++;
    printf("Performing ownership optimization round %d\n",optimize);
    goto optimizeownership;
  }

  free_Ivector(neededvector,1,partitions);

  if(!noopt) free_Ivector(probnodes,1,noknots);
 
  if(info) printf("The partitioning was optimized.\n"); 


  printf("Checking partitioning after optimization\n");
  CheckPartitioning(data,info);

  return(0);
}


#define DEBUG 1
int SaveElmerInputPartitioned(struct FemType *data,struct BoundaryType *bound,
			      char *prefix,int decimals,int halo,int indirect,
			      int parthypre,int info)
/* Saves the mesh in a form that may be used as input 
   in Elmer calculations in parallel platforms. 
   */
{
  int noknots,noelements,sumsides,partitions,hit,maxelemdim,elemdim,parent,parent2;
  int nodesd2,nodesd1,discont,maxelemtype,minelemtype,sidehits,elemsides,side,bctype;
  int part,otherpart,part2,part3,elemtype,sideelemtype,*needednodes,*neededtwice;
  int **bulktypes,*sidetypes,tottypes;
  int i,j,k,l,l2,m,ind,ind2,sideind[MAXNODESD1],sidehit[MAXNODESD1],elemhit[MAXNODESD2];
  char filename[MAXFILESIZE],outstyle[MAXFILESIZE];
  char directoryname[MAXFILESIZE],subdirectoryname[MAXFILESIZE];
  int *neededtimes,*elempart,*elementsinpart,*indirectinpart,*sidesinpart;
  int maxneededtimes,indirecttype,bcneeded,trueparent,*ownerpart;
  int *sharednodes,*ownnodes,reorder,*order,*invorder,*bcnodesaved,*bcnodesaved2,orphannodes;
  int *bcnodedummy,*elementhalo,*neededtimes2;
  int partstart,partfin,filesetsize,nofile,nofile2;
  FILE *out,*outfiles[MAXPARTITIONS+1];


  if(!data->created) {
    printf("You tried to save points that were never created.\n");
    bigerror("No ElmerPost file saved!");
  }

  partitions = data->nopartitions;
  if(!partitions) {
    printf("Tried to save partiotioned format without partitions!\n");
    bigerror("No Elmer mesh files saved!");
  }


  elempart = data->elempart;
  ownerpart = data->nodepart;
  noelements = data->noelements;
  noknots = data->noknots;

  minelemtype = 101;
  maxelemtype = GetMaxElementType(data);
  maxelemdim = GetElementDimension(maxelemtype);
  
  needednodes = Ivector(1,partitions);
  neededtwice = Ivector(1,partitions);
  sharednodes = Ivector(1,partitions);
  ownnodes = Ivector(1,partitions);
  sidetypes = Ivector(minelemtype,maxelemtype);
  bulktypes =  Imatrix(1,partitions,minelemtype,maxelemtype);
  bcnodedummy = Ivector(1,noknots);

  /* Order the nodes so that the different partitions have a continous interval of nodes.
     This information is used only just before the saving of node indexes in each instance. 
     This feature was coded for collaboration with Hypre library that assumes this. */
  reorder = parthypre;
  if(reorder) {
    order = Ivector(1,noknots);
    k = 0;
    for(j=1;j<=partitions;j++)
      for(i=1; i <= noknots; i++) 
	if(ownerpart[i] == j) {
	  k++;
	  order[i] = k; 
	}
    invorder = Ivector(1,noknots);
    for(i=1;i<=noknots;i++) 
      invorder[order[i]] = i;
  } 

  sprintf(directoryname,"%s",prefix);
  sprintf(subdirectoryname,"%s.%d","partitioning",partitions);

#ifdef MINGW32
  mkdir(directoryname);
#else
  mkdir(directoryname,0700);
#endif
  chdir(directoryname);
#ifdef MINGW32
  mkdir(subdirectoryname);
#else
  mkdir(subdirectoryname,0700);
#endif

  chdir(subdirectoryname);

  if(info) printf("Saving mesh in parallel ElmerSolver format to directory %s/%s.\n",
		  directoryname,subdirectoryname);

  filesetsize = MAXPARTITIONS;
  if(partitions > filesetsize) 
    if(info) printf("Saving %d partitions in maximum sets of %d\n",partitions,filesetsize);

  elementsinpart = Ivector(1,partitions);
  indirectinpart = Ivector(1,partitions);
  sidesinpart = Ivector(1,partitions);
  elementhalo = Ivector(1,partitions);
  for(i=1;i<=partitions;i++)
    elementsinpart[i] = indirectinpart[i] = sidesinpart[i] = elementhalo[i] = 0;

  for(j=1;j<=partitions;j++)
    for(i=minelemtype;i<=maxelemtype;i++)
      bulktypes[j][i] = 0;

  /* Compute how many times a node may be needed at maximum */
  maxneededtimes = data->maxpartitiontable;
  neededtimes = Ivector(1,noknots);
  for(i=1;i<=noknots;i++) {
    neededtimes[i] = 0;
    for(j=1;j<=maxneededtimes;j++)
      if(data->partitiontable[j][i]) neededtimes[i] += 1;
  }
  if(info) printf("Nodes belong to %d partitions in maximum\n",maxneededtimes);


  /*********** part.n.elements *********************/
  /* Save elements in all partitions and where they are needed */

  
  partstart = 1;
  partfin = MIN( partitions, filesetsize );

 next_elements_set:

  for(part=partstart;part<=partfin;part++) {
    sprintf(filename,"%s.%d.%s","part",part,"elements");
    nofile = part - partstart + 1;
    outfiles[nofile] = fopen(filename,"w");
  }

  for(i=1;i<=noelements;i++) {
    part = elempart[i];

    elemtype = data->elementtypes[i];
    nodesd2 = elemtype%100;

    if(part >= partstart && part <= partfin) {
      nofile = part - partstart + 1;
      bulktypes[part][elemtype] += 1;
      elementsinpart[part] += 1;
    
      if(halo)
	fprintf(outfiles[nofile],"%d/%d %d %d ",i,part,data->material[i],elemtype);
      else
	fprintf(outfiles[nofile],"%d %d %d ",i,data->material[i],elemtype);

      for(j=0;j < nodesd2;j++) {
	ind = data->topology[i][j];
	if(reorder) ind = order[ind];
	fprintf(outfiles[nofile],"%d ",ind);
      }
      fprintf(outfiles[nofile],"\n");    
    }


    if(halo) {
      /* The face can be shared only if there are enough shared nodes */
      otherpart = 0;
      for(j=0;j < nodesd2;j++) {
	ind = data->topology[i][j];
	if(neededtimes[ind] > 1) otherpart++;
      }
      if(!otherpart) continue;

      /* If the saving of halo is requested check it for elements which have at least 
	 two nodes in shared partitions. First make this quick test. */
      elemsides = elemtype / 100;
      if(elemsides == 8) {
	if(otherpart < 4) continue;
	elemsides = 6;
      }
      else if(elemsides == 6) {
	if(otherpart < 3) continue;
	elemsides = 5;
      }
      else if(elemsides == 5) {
	if(otherpart < 3) continue;
	elemsides = 4;
      }      
      else 
	if(otherpart < 2) continue;

      /* In order for the halo to be present the element should have a boundary 
	 fully immersed in the other partition. This test takes more time. */

      for(side=0;side<elemsides;side++) {
	GetElementSide(i,side,1,data,&sideind[0],&sideelemtype);

	for(l=1;l<=neededtimes[sideind[0]];l++) {
	  part2 = data->partitiontable[l][sideind[0]];
	  if(part2 == part) continue;

	  sidehits = 1;
	  for(k=1;k<sideelemtype%100;k++) {
	    for(l2=1;l2<=neededtimes[sideind[k]];l2++) {
	      part3 = data->partitiontable[l2][sideind[k]];
	      if(part2 == part3) sidehits++;
	    }
	  }

	  if(part2 < partstart || part2 > partfin) continue;
	  nofile2 = part2 - partstart + 1;


	  if(sidehits == sideelemtype % 100 && elementhalo[part2] != i) {
	    if(0) printf("Adding halo for partition %d and element %d\n",part2,i);

	    /* Remember that this element is saved for this partition */
	    elementhalo[part2] = i;

	    fprintf(outfiles[nofile2],"%d/%d %d %d ",i,part,data->material[i],elemtype);

	    for(j=0;j < nodesd2;j++) {
	      ind = data->topology[i][j];
	      if(reorder) ind = order[ind];
	      fprintf(outfiles[nofile2],"%d ",ind);
	    }
	    fprintf(outfiles[nofile2],"\n");    	    
	    bulktypes[part2][elemtype] += 1;
	    elementsinpart[part2] += 1;	

	    /* Add the halo on-the-fly */
	    
	    for(j=0;j < nodesd2;j++) {
	      ind = data->topology[i][j];
	      hit = FALSE;
	      for(k=1;k<=maxneededtimes;k++) {
		part3 = data->partitiontable[k][ind];
		if(part3 == part2) hit = TRUE;
		if(!part3) break;
	      }
	      if(!hit) {
		if(k <= maxneededtimes) {
		  data->partitiontable[k][ind] = part2;
		} 
		else {
		  maxneededtimes++;
		  if(0) printf("Allocating new column %d in partitiontable\n",maxneededtimes);
		  data->partitiontable[maxneededtimes] = Ivector(1,noknots);
		  for(m=1;m<=noknots;m++)
		    data->partitiontable[maxneededtimes][m] = 0;
		  data->partitiontable[maxneededtimes][ind] = part2;
		}
	      }
	    }

 
	  }
	}  
      }
    }
  }

  for(part=partstart;part<=partfin;part++) {
    nofile = part - partstart + 1;
    fclose(outfiles[nofile]);
  }
  if(partfin < partitions) {
    partstart = partfin + 1;
    partfin = MIN( partfin + filesetsize, partitions);
    goto next_elements_set;
  }
  /* part.n.elements saved */


  /* The partitiontable has been changed to include the halo elements. The need for saving the 
     halo nodes may be checked by looking whether the number of how many partitions needs 
     the element has changed. */
  if(halo) {
    int halonodes;
    neededtimes2 = Ivector(1,noknots);
    halonodes = 0;

    for(i=1;i<=noknots;i++) {
      neededtimes2[i] = 0;
      for(j=1;j<=maxneededtimes;j++) 
	if(data->partitiontable[j][i]) neededtimes2[i] += 1;
      halonodes += neededtimes2[i] - neededtimes[i];
    }
    if(data->maxpartitiontable < maxneededtimes) {
      data->maxpartitiontable = maxneededtimes;
      if(info) printf("With the halos nodes belong to %d partitions in maximum\n",maxneededtimes);
    }
    if(info) printf("There are %d additional shared nodes resulting from the halo.\n",halonodes);
  }
  else {
    neededtimes2 = neededtimes;
  }

  /* Define new BC numbers for indirect connections. These should not be mixed with
     existing BCs as they only serve the purpose of automatically creating the matrix structure. */
  if(indirect) {
    indirecttype = 0;
    for(j=0;j < MAXBOUNDARIES;j++) 
      for(i=1; i <= bound[j].nosides; i++) 
	if(bound[j].types[i] > indirecttype) indirecttype = bound[j].types[i];
    indirecttype++;
    if(info) printf("Indirect connections given index %d and elementtype 102.\n",indirecttype);
  }

  /* The output format is the same for all partitions */
  if(data->dim == 2) 
    sprintf(outstyle,"%%d %%d %%.%dlg %%.%dlg 0.0\n",decimals,decimals);
  else 
    sprintf(outstyle,"%%d %%d %%.%dlg %%.%dlg %%.%dlg\n",decimals,decimals,decimals);

  if(info) printf("Saving mesh for %d partitions\n",partitions);


  /*********** part.n.nodes *********************/

  partstart = 1;
  partfin = MIN( partitions, filesetsize);

  for(i=1;i<=partitions;i++) {
    needednodes[i] = 0;
    neededtwice[i] = 0;
    sharednodes[i] = 0;
    ownnodes[i] = 0;
  }    

 next_nodes_set:

  for(part=partstart;part<=partfin;part++) {
    sprintf(filename,"%s.%d.%s","part",part,"nodes");
    nofile = part - partstart + 1;
    outfiles[nofile] = fopen(filename,"w");
  }
  
  for(l=1; l <= noknots; l++) {      
    i = l;
    if(reorder) i=invorder[l];

    /*    for(j=1;j<=neededtimes2[i];j++) { */
    for(j=1;j<=maxneededtimes;j++) {

      k = data->partitiontable[j][i];
      if(!k) break;

      if(k < partstart || k > partfin) continue;
      nofile = k - partstart + 1;

      ind = i;
      if(reorder) ind=order[i];

      if(data->dim == 2)
	fprintf(outfiles[nofile],outstyle,ind,-1,data->x[i],data->y[i]);
      else if(data->dim == 3)
	fprintf(outfiles[nofile],outstyle,ind,-1,data->x[i],data->y[i],data->z[i]);	  	    
      
      needednodes[k] += 1;
      if(k == ownerpart[i]) 
	ownnodes[k] += 1;
      else 
	sharednodes[k] += 1;
    }
  }

  for(part=partstart;part<=partfin;part++) {
    nofile = part - partstart + 1;
    fclose(outfiles[nofile]);
  }
  if(partfin < partitions) {
    partstart = partfin + 1;
    partfin = MIN( partfin + filesetsize, partitions);
    goto next_nodes_set;
  }
  /* part.n.nodes saved */
      

  /*********** part.n.shared *********************/

  partstart = 1;
  partfin = MIN( partitions, filesetsize );

 next_shared_set:

  for(part=partstart;part<=partfin;part++) {
    sprintf(filename,"%s.%d.%s","part",part,"shared");
    nofile = part - partstart + 1;
    outfiles[nofile] = fopen(filename,"w");
  }

  for(l=1; l <= noknots; l++) {      
    i = l;
    if(reorder) i = invorder[l];

    if(neededtimes2[i] <= 1) continue;

    for(j=1;j<=neededtimes2[i];j++) {
      k = data->partitiontable[j][i];
      
      if(k < partstart || k > partfin) continue;
      nofile = k - partstart + 1;

      ind = i;
      if(reorder) ind = order[i];
      neededtwice[k] += 1; 

      fprintf(outfiles[nofile],"%d %d %d",ind,neededtimes2[i],ownerpart[i]);      
      for(m=1;m<=neededtimes2[i];m++) 
	if(data->partitiontable[m][i] != ownerpart[i]) fprintf(outfiles[nofile]," %d",data->partitiontable[m][i]);
      fprintf(outfiles[nofile],"\n");
    }
  }

  for(part=partstart;part<=partfin;part++) {
    nofile = part - partstart + 1;
    fclose(outfiles[nofile]);
  }
  if(partfin < partitions) {
    partstart = partfin + 1;
    partfin = MIN( partfin + filesetsize, partitions);
    goto next_shared_set;
  }
  /* part.n.shared saved */




   
  /*********** part.n.boundary *********************/
  /* This is still done in partition loop as the subroutines are quite complicated */

  bcnodesaved = bcnodedummy;
  bcnodesaved2 = Ivector(1,data->noknots);

  for(part=1;part<=partitions;part++) { 
    sprintf(filename,"%s.%d.%s","part",part,"boundary");
    out = fopen(filename,"w");

    for(i=1;i<=noknots;i++)
      bcnodesaved[i] = bcnodesaved2[i] = FALSE;
   
    for(i=minelemtype;i<=maxelemtype;i++)
      sidetypes[i] = 0;
    
    sumsides = 0;
    for(j=0;j < MAXBOUNDARIES;j++) {
      
      /* Normal boundary conditions */
      for(i=1; i <= bound[j].nosides; i++) {
	
	GetElementSide(bound[j].parent[i],bound[j].side[i],bound[j].normal[i],
		       data,sideind,&sideelemtype);
	bctype = bound[j].types[i];
	nodesd1 = sideelemtype%100;

	bcneeded = 0;
	for(l=0;l<nodesd1;l++) {
	  ind = sideind[l];
	  for(k=1;k<=neededtimes2[ind];k++)
	    if(part == data->partitiontable[k][ind]) bcneeded++;
	}
	if(bcneeded != nodesd1) continue;

	/* Check whether the side is such that it belongs to the domain */
	trueparent = (elempart[bound[j].parent[i]] == part);
	if(bound[j].ediscont) 
	  discont = bound[j].discont[i];
	else 
	  discont = FALSE;

	if(!trueparent && !discont) {
	  if(bound[j].parent2[i]) 
	    trueparent = (elempart[bound[j].parent2[i]] == part);
	}	
	if(!trueparent && !halo) continue;

	sumsides++;	
	sidetypes[sideelemtype] += 1;
	elemdim = GetElementDimension(sideelemtype);

	parent = bound[j].parent[i];
	parent2 = bound[j].parent2[i];

	/* The need of parents for DIM-2 boundaries may just create extra work */
	/* if(maxelemdim > elemdim + 1) parent = parent2 = 0; */

	if(halo) {
	  if(trueparent)
	    fprintf(out,"%d/%d %d %d %d %d",
		    sumsides,part,bctype,parent,parent2,sideelemtype);
	  else
	    fprintf(out,"%d/%d %d %d %d %d",
		    sumsides,elempart[bound[j].parent[i]],bctype,parent,parent2,sideelemtype);	    
	}
	else {
	  fprintf(out,"%d %d %d %d %d",
		  sumsides,bctype,parent,parent2,sideelemtype);	  
	}
	if(reorder) {
	  for(l=0;l<nodesd1;l++)
	    fprintf(out," %d",order[sideind[l]]);
	} else {
	  for(l=0;l<nodesd1;l++)
	    fprintf(out," %d",sideind[l]);	  
	}
	fprintf(out,"\n");

	/* Memorize that the node has already been saved as a regular BC. */
	for(l=0;l<nodesd1;l++) {
	  k = sideind[l];
	  if(bcnodesaved[k] == bctype || bcnodesaved2[k] == bctype ) continue;
	  
	  if(!bcnodesaved[k]) 
	    bcnodesaved[k] = bctype;
	  else if(!bcnodesaved2[k]) 
	    bcnodesaved2[k] = bctype;
	  else 
	    if(0) printf("Node %d shared by more than two BCs (%d)\n",k,bctype);
	}
      }
    }

    /* These are orphan nodes that are saved as 101 points and may be given 
       Dirichlet conditions in the code. If the node is already saved in respect to 
       this partition no saving is done. */

    orphannodes = 0;
    for(j=0;j < MAXBOUNDARIES;j++) {
      for(i=1; i <= bound[j].nosides; i++) {      
	GetElementSide(bound[j].parent[i],bound[j].side[i],bound[j].normal[i],
		       data,sideind,&sideelemtype);
	nodesd1 = sideelemtype%100;
	bctype = bound[j].types[i];
	
	for(l=0;l<nodesd1;l++) {
	  ind = sideind[l];
	  for(k=1;k<=neededtimes[ind];k++)
	    if(part == data->partitiontable[k][ind]) {

	      /* Check whether the side is such that it belongs to the domain,
		 if it does it cannot be an orphan node. */
	      if( elempart[bound[j].parent[i]] == part) continue;
	      if( elempart[bound[j].parent2[i]] == part) continue;
	      
	      /* Check whether the nodes was not already saved */
	      if( bcnodesaved[ind] == bctype ) continue;	  
	      if( bcnodesaved2[ind] == bctype ) continue;	  

	      /* Memorize if the node really was saved. */
	      if(!bcnodesaved[ind]) 
		bcnodesaved[ind] = bctype;
	      else if(!bcnodesaved2[ind]) 
		bcnodesaved2[ind] = bctype;
		
	      orphannodes++;
	      
	      sumsides++;
	      sidetypes[101] += 1;

	      if(reorder) {
		fprintf(out,"%d %d 0 0 101 %d\n",sumsides,bctype,order[ind]);
	      }
	      else {
		fprintf(out,"%d %d 0 0 101 %d\n",sumsides,bctype,ind);
	      }	  
	    }
	}
      }
    }      
    if(0 && orphannodes) printf("There were %d orphan BC nodes in partition %d\n",orphannodes,part);

    /* The second side for discontinuous boundary conditions.
       Note that this has not been treated for orphan control. */
    for(j=0;j < MAXBOUNDARIES;j++) {
      for(i=1; i <= bound[j].nosides; i++) {
	if(bound[j].ediscont) 
	  discont = bound[j].discont[i];
	else 
	  discont = FALSE;
	
	if(!bound[j].parent2[i] || !discont) continue;
	
	GetElementSide(bound[j].parent2[i],bound[j].side2[i],-bound[j].normal[i],
		       data,sideind,&sideelemtype); 
	nodesd1 = sideelemtype%100;	
	
	bcneeded = 0;
	for(l=0;l<nodesd1;l++) {
	  ind = sideind[l];
	  for(k=1;k<=neededtimes[ind];k++)
	    if(part == data->partitiontable[k][ind]) bcneeded++;
	}
	if(bcneeded < nodesd1) continue;
	
	trueparent = (elempart[bound[j].parent2[i]] == part);
	if(!trueparent) continue;
	
	sumsides++;
	fprintf(out,"%d %d %d %d ",
		sumsides,bound[j].types[i],bound[j].parent2[i],bound[j].parent[i]);
	
	fprintf(out,"%d ",sideelemtype);
	sidetypes[sideelemtype] += 1;
	if(reorder) {
	  for(l=0;l<nodesd1;l++)
	    fprintf(out,"%d ",order[sideind[l]]);
	} 
	else {
	  for(l=0;l<nodesd1;l++)
	    fprintf(out,"%d ",sideind[l]);	  
	} 
	fprintf(out,"\n");
      }
    }
    sidesinpart[part] = sumsides;
        

    /* Boundary nodes that express indirect couplings between different partitions.
       This makes it possible for ElmerSolver to create a matrix connection that 
       is known to exist. */

    if (indirect) {
      int maxsides,nodesides,maxnodeconnections,connectednodes,m;
      int **nodepairs,*nodeconnections,**indpairs;      

      nodeconnections = bcnodedummy;
      l = 0;
      maxsides = 0;
      nodesides = 0;

  findindirect:

      /* First calculate the maximum number of additional sides */
      for(i=1;i<=noelements;i++) {

	/* owner partition cannot cause an indirect coupling */
	if(elempart[i] == part) continue;
	
	elemtype = data->elementtypes[i];
	nodesd2 = elemtype%100;
	
	/* Check how many nodes still belong to this partition, 
	   if more than one there may be indirect coupling. */
	for(j=0;j < nodesd2;j++) {
	  elemhit[j] = FALSE;
	  ind = data->topology[i][j];
	  for(k=1;k<=neededtimes[ind];k++) 
	    if(part == data->partitiontable[k][ind]) elemhit[j] = TRUE;
	}
	bcneeded = 0;
	for(j=0;j < nodesd2;j++) 
	  if(elemhit[j]) bcneeded++;
	if(bcneeded <= 1) continue;
	
	if(l == 0) {
	  maxsides += (bcneeded-1)*bcneeded/2;
	} 
	else {
	  for(j=0;j < nodesd2;j++) {	  
	    for(k=j+1;k < nodesd2;k++) {
	      if(elemhit[j] && elemhit[k]) {
		nodesides += 1;

		/* The minimum index always first */
		if(data->topology[i][j] <= data->topology[i][k]) {
		  nodepairs[nodesides][1] = data->topology[i][j];
		  nodepairs[nodesides][2] = data->topology[i][k];
		}
		else {
		  nodepairs[nodesides][1] = data->topology[i][k];
		  nodepairs[nodesides][2] = data->topology[i][j];		  
		}
	      }
	    }
	  }
	}
      }

      /* After first round allocate enough space to memorize all indirect non-element couplings. */      
      if(l == 0) {
	nodepairs = Imatrix(1,maxsides,1,2);
	for(i=1;i<=maxsides;i++)
	  nodepairs[i][1] = nodepairs[i][2] = 0;
	l++;
	goto findindirect;
      }
      if(0) printf("Number of non-element connections is %d\n",nodesides);
      
      
      for(i=1;i<=noknots;i++)
	nodeconnections[i] = 0;
      
      for(i=1;i<=nodesides;i++)
	nodeconnections[nodepairs[i][1]] += 1;
      
      maxnodeconnections = 0;
      for(i=1;i<=noknots;i++)
	maxnodeconnections = MAX(maxnodeconnections, nodeconnections[i]);     
      if(0) printf("Maximum number of node-to-node connections %d\n",maxnodeconnections);

      connectednodes = 0;
      for(i=1;i<=noknots;i++) {
	if(nodeconnections[i] > 0) {
	  connectednodes++;
	  nodeconnections[i] = connectednodes;
	}
      }
      if(0) printf("Number of nodes with non-element connections %d\n",connectednodes);

      indpairs = Imatrix(1,connectednodes,1,maxnodeconnections);
      for(i=1;i<=connectednodes;i++)
	for(j=1;j<=maxnodeconnections;j++)
	  indpairs[i][j] = 0;
      
      for(i=1;i<=nodesides;i++) {
	ind = nodeconnections[nodepairs[i][1]];
	for(j=1;j<=maxnodeconnections;j++) {
	  if(indpairs[ind][j] == 0) {
	    indpairs[ind][j] = i;	    
	    break;
	  }
	}
      }

      /* Remove dublicate connections */
      l = 0;
      for(i=1;i<=connectednodes;i++) {
	for(j=1;j<=maxnodeconnections;j++)
	  for(k=j+1;k<=maxnodeconnections;k++) {
	    ind = indpairs[i][j];
	    ind2 = indpairs[i][k];
	    if(!ind || !ind2) continue;
	    
	    if(!nodepairs[ind][1] || !nodepairs[ind][2]) continue;

	    if(nodepairs[ind][2] == nodepairs[ind2][2]) {
	      nodepairs[ind2][1] = nodepairs[ind2][2] = 0;
	      l++;
	    }
	  }
      }
      if(0) printf("Removed %d duplicate connections\n",l);

      
      /* Remove connections that already exist */
      m = 0;
      for(i=1;i<=noelements;i++) {
	if(elempart[i] != part) continue;
	
	elemtype = data->elementtypes[i];
	nodesd2 = elemtype%100;
	
	for(j=0;j < nodesd2;j++) {
	  ind = nodeconnections[data->topology[i][j]];
	  if(!ind) continue;
	  
	  for(k=0;k < nodesd2;k++) {
	    if(j==k) continue;
	    
	    for(l=1;l<=maxnodeconnections;l++) {
	      ind2 = indpairs[ind][l];
	      if(!ind2) break;

	      if(nodepairs[ind2][1] == data->topology[i][j] && nodepairs[ind2][2] == data->topology[i][k]) {
		nodepairs[ind2][1] = nodepairs[ind2][2] = 0;	    
		m++;
	      }
	    }
	  }
	}
      }
      if(0) printf("Removed %d connections that already exists in other elements\n",m);
      
      for(i=1; i <= nodesides; i++) {
	ind = nodepairs[i][1]; 
	ind2 = nodepairs[i][2];
	if(!ind || !ind2) continue;	
	sumsides++;

	sideelemtype = 102;
	if(reorder) {
	  fprintf(out,"%d %d %d %d %d %d %d\n",
		  sumsides,indirecttype,0,0,sideelemtype,order[ind],order[ind2]);
	} else {
	  fprintf(out,"%d %d %d %d %d %d %d\n",
		  sumsides,indirecttype,0,0,sideelemtype,ind,ind2);	  
	}
	sidetypes[sideelemtype] += 1;
	indirectinpart[part] += 1;	
      }

      /* Finally free some extra space that was allocated */
      free_Imatrix(indpairs,1,connectednodes,1,maxnodeconnections);
      free_Imatrix(nodepairs,1,maxsides,1,2);
    }
    /* End of indirect couplings */


    fclose(out);
    /*********** end of part.n.boundary *********************/



    
    /*********** start of part.n.header *********************/
    tottypes = 0;
    for(i=minelemtype;i<=maxelemtype;i++) {
      if(bulktypes[part][i]) tottypes++;
      if(sidetypes[i]) tottypes++;
    }

    sprintf(filename,"%s.%d.%s","part",part,"header");
    out = fopen(filename,"w");
    fprintf(out,"%-6d %-6d %-6d\n",
	    needednodes[part],elementsinpart[part],sumsides);

    fprintf(out,"%-6d\n",tottypes);
    for(i=minelemtype;i<=maxelemtype;i++) 
      if(bulktypes[part][i]) 
	fprintf(out,"%-6d %-6d\n",i,bulktypes[part][i]);

    for(i=minelemtype;i<=maxelemtype;i++) 
      if(sidetypes[i]) 
	fprintf(out,"%-6d %-6d\n",i,sidetypes[i]);

    fprintf(out,"%-6d %-6d\n",neededtwice[part],0);
    fclose(out);

    if(info) {
      if(part == 1) {
	printf("   %-5s %-10s %-10s %-8s %-8s %-8s",
			   "part","elements","nodes","shared","bc elems","orphan");
	if(indirect) printf(" %-8s","indirect");
	printf("\n");
      }
      printf("   %-5d %-10d %-10d %-8d %-8d %-8d",
	     part,elementsinpart[part],ownnodes[part],sharednodes[part],sidesinpart[part],orphannodes);
      if(indirect) printf(" %-8d",indirectinpart[part]);
      printf("\n");
    }
  } 
  /*********** end of part.n.header *********************/


  free_Ivector(bcnodesaved2,1,noknots);
  if(halo) free_Ivector(neededtimes2,1,noknots);
  
  
  chdir("..");
  chdir("..");

  if(reorder) free_Ivector(order,1,noknots);
  free_Ivector(needednodes,1,partitions);
  free_Ivector(neededtwice,1,partitions);
  free_Ivector(sharednodes,1,partitions);
  free_Ivector(ownnodes,1,partitions);
  free_Ivector(sidetypes,minelemtype,maxelemtype);
  free_Imatrix(bulktypes,1,partitions,minelemtype,maxelemtype);
  
  if(info) printf("Writing of partitioned mesh finished\n");
  
  return(0);
}


#if PARTMETIS 
int ReorderElementsMetis(struct FemType *data,int info)
/* Calls the fill reduction ordering algorithm of Metis library. */
{
  int i,j,k,l,nn,totcon,maxcon,con,options[8];
  int noelements,noknots,nonodes;
  int *xadj,*adjncy,numflag,*perm,*iperm,**newtopology;
  Real *newx,*newy,*newz;

  noelements = data->noelements;
  noknots = data->noknots;

  if(info) printf("Reordering %d knots and %d elements using Metis reordering routine.\n",
		  noknots,noelements);
  i = CalculateIndexwidth(data,FALSE,perm);
  if(info) printf("Indexwidth of the original node order is %d.\n",i);


  CreateDualGraph(data,TRUE,info);
  maxcon = data->dualmaxconnections;

  totcon = 0;
  for(i=1;i<=noknots;i++) {
    for(j=0;j<maxcon;j++) {
      con = data->dualgraph[j][i];
      if(con) totcon++;
    }
  }
  if(info) printf("There are %d connections alltogether\n",totcon);

  xadj = Ivector(0,noknots);
  adjncy = Ivector(0,totcon-1);
  for(i=0;i<totcon;i++) 
    adjncy[i] = 0;

  totcon = 0;
  for(i=1;i<=noknots;i++) {
    xadj[i-1] = totcon;
    for(j=0;j<maxcon;j++) {
      con = data->dualgraph[j][i];
      if(con) {
	adjncy[totcon] = con-1;
	totcon++;
      }
    }
  }
  xadj[noknots] = totcon;

  nn = noknots;
  numflag = 0;
  for(i=0;i<8;i++) options[i] = 0;
  perm = Ivector(0,noknots-1);
  iperm = Ivector(0,noknots-1);
  
  if(info) printf("Starting Metis reordering routine.\n");

  METIS_NodeND(&nn,xadj,adjncy,&numflag,&options[0],perm,iperm);

  if(info) printf("Finished Metis reordering routine.\n");

  if(info) printf("Moving knots to new positions\n");
  newx = Rvector(1,data->noknots);
  newy = Rvector(1,data->noknots);
  if(data->dim == 3) newz = Rvector(1,data->noknots);

  for(i=1;i<=data->noknots;i++) {
    newx[i] = data->x[perm[i-1]+1];
    newy[i] = data->y[perm[i-1]+1];
    if(data->dim == 3) newz[i] = data->z[perm[i-1]+1];
  }

  free_Rvector(data->x,1,data->noknots);
  free_Rvector(data->y,1,data->noknots);
  if(data->dim == 3) free_Rvector(data->z,1,data->noknots);

  data->x = newx;
  data->y = newy;
  if(data->dim == 3) data->z = newz;


  if(info) printf("Chanching the element topology\n");

  newtopology = Imatrix(1,noelements,0,data->maxnodes-1);

  for(j=1;j<=noelements;j++) {
    nonodes = data->elementtypes[j]%100;
    for(i=0;i<nonodes;i++) {
      k = data->topology[j][i];
      newtopology[j][i] = iperm[k-1]+1;
    }
  }
  free_Imatrix(data->topology,1,noelements,0,data->maxnodes-1);
  data->topology = newtopology;

  i = CalculateIndexwidth(data,FALSE,perm);
  if(info) printf("Indexwidth of the new node order is %d.\n",i);

  if(0) printf("Deallocating vectors needed for reordering.\n");
  free_Ivector(iperm,0,noknots-1);
  free_Ivector(perm,0,noknots-1);

  return(0);
}
#endif
