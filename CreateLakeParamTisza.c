/******************************************************************************
   SUMMARY:  
   This program is designed to create a lake parameter file for use with the VIC
   version 4.1.1 and higher.  It reads in both an ascii dem of a VIC grid cell 
   (in arc/info ascii format with standard 6 line header) and a classified land 
   use map (in arc/info ascii format with standard 6 line header) to calculate 
   either the elevation distribution versus area (VIC 4.1.1 lake parameter 
   format) or the topographic wetness index distribution versus area 
   (experimental VIC 4.1.2_SEA format) for wetland areas only.

   The classification code in the land cover file for wetland and open water 
   areas must be specified here (the codes associated with all other 
   vegetation types do not matter): */

#define WETLANDTHRESH 13552  /* modified 12 to 141 to fit ACRE*/
#define WATERTHRESH 216623    
   
/******************************************************************************

   The program uses the method presented by Pelletier (2008) to fill sinks and 
   eliminate errors in flat areas by calculating flow accumulation using 
   a multiple flow direction algorithm. 

   REFERENCES: Jon Pelletier (2008) Quantitative Modeling of Earth Surface Processes. 
  
   USAGE: CreatLakeParam <DEM file> <Grid no>  <SEA flag> ;
     DEM file: Name of DEM (elevation) floating point grid with arcinfo header
     Gridno: integer - the number of the VIC grid cell for the parameter file
     veg file: Name of land cover integer grid with arcinfo header
     SEA flag: "SEA" for output in SEA code file format; "LAKE" for original lake model format

   AUTHOR:       Chun-Mei Chiu / Laura Bowling
   DESCRIPTION:                  
   Usage: 
   Compile with: gcc CreateLakeParam.c -lm -o CreateLakeParam
                 
   COMMENTS:
   Modified: 4/22/2011


*******************************************************************************/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include<malloc.h>

/* This part is used to check time spent in various functions. */ 
#include <sys/param.h>
#include <sys/times.h>
#include <sys/types.h>
struct tms tt,uu;

#define MAXSTRING 500
#define NNEIGHBORS  8
#define VERTRES 2.3       /* assumed vertical resolution of the dem (m) */
#define OUTSIDEBASIN -99

#define SWAP(a,b) itemp=(a);(a)=(b);(b)=itemp;
#define M 7
#define NSTACK 10000000

#define FREE_ARG char*
#define NR_END 1
#define fillincrement 0.01
#define oneoversqrt2 0.707106781187
double **topo,**flow,**flow1,**flow2,**flow3,**flow4,**flow5,**flow6,**flow7,**flow8;
int *iup,*idown,*jup,*jdown;

typedef struct 
{
  double Rank;
  double x;
  double y;
}ITEM;


/*--- Function Declaration---*/ 
void quick(ITEM *item, int count);
void qs(ITEM *item, int left, int right);
void Topindex(double **dem, double **flowacc, int columns, int rows, double xorig, double yorig, double deltax, double deltay, double nodata, char gridno[], char option[]);
void VICcalculation(double** iniarray, int n, int m, char gridno[], float wetlandVeg, float waterVeg, int totalVeg, double deltax, double deltay, char option[], double ElevRange);
double correlation(double *AREASUM, double *DEMSUM, int counter7);
void PrintResult(FILE *file, int columns, int rows, double xorig, double yorig, double deltax, double deltay, double nodata);
double **Memoryalloc(int columns, int rows);

/* for contributing area */
void fillin(double **dem, int columns, int rows, double deltax, double deltay, double nodata);
int *ivector(long nl, long nh);
double *vector(long nl, long nh);
void free_ivector(int *v, long nl, long nh);
void free_vector(double *v, long nl, long nh);
double **matrix(int nrl, int nrh, int ncl,int nch);
void indexx(int n,double arr[], int indx[]);
void setupgridneighbors(int lattice_size_x,int lattice_size_y);
void fillinpitsandflats(int i,int j, int lattice_size_x,int lattice_size_y, double nodata);
void mfdflowroute(int i,int j, double nodata);
double get_dist(double, double, double, double);

int main(int argc ,char *argv[])  
{
  FILE   *fdem, *fo, *fv;
  char tempstr[MAXSTRING];         
  char   demfile[1000], option[1000], gridno[1000], vegfile[1000];
  int    columns, rows, lattice_size_x, lattice_size_y;   
  int    i, j, cnt;  /* counters */
  double xorig, yorig, deltax, deltay, delta, nodata;  
  double **dem, **flowacc; 
  int temp;
  double celllat, celllong;
  double newlat, newlong;

  /* test time in each step */
  long time_begin,time_end;
  float elapsed_time; 
  long time_begin2,time_end2;
  float elapsed_time2; 


  /*-------------print the usage ------------------*/  
  if (argc != 4)
    {
      printf("Usage: CreatLakeParam <DEM file> <Grid no> <vegetation file> <SEA flag> \n");
      printf("\t\t DEM file : DEM(elevation) floating point grid with arcinfo header;\n");
      printf("\t\t Gridno : the number of each VIC grid cell\n");
      printf("\t\t SEA flag: SEA for SEA code file format; LAKE for original lake model format);\n");
      exit(0);
    }
  strcpy(demfile, argv[1]);
  strcpy(gridno, argv[2]);
  strcpy(option, argv[3]);

  /*-----------------------------------------------*/
  /*	 OPEN FILES*/
  /*-----------------------------------------------*/  
  if((fdem=fopen(demfile,"r"))==NULL)
    { 
      //printf("cannot open/read dem file,%s\n",demfile);
      exit(1);
    }

  /* check data file has data inside */ 
  if ( getc(fdem) == EOF)
    exit(0);
  

  /*----------------------------------------------*/
  /*Scan and read in DEM's and VEG's header*/
  /*----------------------------------------------*/  
  fscanf(fdem,"%s %d",tempstr,&columns);
  fscanf(fdem,"%s %d",tempstr,&rows);
  fscanf(fdem,"%s %lf",tempstr,&xorig);
  fscanf(fdem,"%s %lf",tempstr,&yorig);
  fscanf(fdem,"%s %lf",tempstr,&delta);
  fscanf(fdem,"%s %lf",tempstr,&nodata);

  celllat = yorig + delta*rows/2;
  newlat = celllat + delta;
  celllong = xorig + delta*columns/2;
  newlong = celllong + delta;

  deltax = 1000.*get_dist(celllat, celllong, celllat, newlong);
  deltay = 1000.*get_dist(celllat, celllong, newlat, celllong);

  lattice_size_x = columns;
  lattice_size_y = rows;

  /*----------------------------------------------*/
  /* Allocate memory to arrays for handling huge data */
  /*----------------------------------------------*/  
 
  dem = Memoryalloc(columns, rows);
  flowacc = Memoryalloc(columns, rows);

  /*-----------------------------------------------*/
  /* READ IN DEM's Mask FILES                      */
  /*---------------------------------------------- */
  for(i=0; i<rows;i++) 
    {
      for(j=0; j<columns; j++)
	{
	  fscanf(fdem,"%lf",&dem[i][j]);
	  if(dem[i][j] < 0)
	    {
	      dem[i][j] = nodata;  //check the dem file
	    }
	  //fprintf(stderr, "1 %d\n",veg[i][j]); 
	} 
    }
  

  /***********************************/
  /*  fill and calculate multi flow accumulation from dem.    */
  /*  Creates filled dem (topo) and accumulation grid (flow). */
  /***********************************/
  
  fillin(dem, columns, rows, deltax, deltay, nodata);
 
  /*************************************/
  /* wetness index calculation         */
  /*************************************/

  /* Replace dem with filled dem. Plletier code indexes arrays starting at 1, so offset is needed. */
  for (i = 0; i < rows; i++) {
    for (j = 0; j < columns; j++){      
      dem[i][j] = topo[j+1][i+1];
      flowacc[i][j] = flow[j+1][i+1];
      if (dem[i][j] == nodata )
	cnt++;
    }
  }
  
  /* Check to make sure dem contains some data. */
  if(cnt < rows*columns)
    { 
      /* This will generate the 526x526 grid lake paramater */
      //      time_begin2 = times(&tt);
      Topindex(dem, flowacc, columns, rows, xorig, yorig, deltax, deltay, nodata, gridno, option);
      //  time_end2 = times(&uu);
      //  elapsed_time2= (float)(time_end2-time_begin2)/HZ ;
     }
  else {
    printf("No valid value in this grid %s\n", gridno);
  }
  cnt = 0;


  /*  free memory */
  for (i = 0; i < rows; i++)
    {
      free(dem[i]);
    }
    free(dem);

  return;
} /*END OF MAIN FUNCTION*/

/*****************************************************************************/
/*   Topindex Function                                                       */
/*****************************************************************************/
void Topindex(double **dem, double **flowacc, int columns, int rows, double xorig , 
	      double yorig, double deltax, double deltay, double nodata, char gridno[],
	      char option[])
{ 
  int xneighbor[NNEIGHBORS] = { -1, 0, 1, 1, 1, 0, -1, -1 }; /*8 neighbor*/
  int yneighbor[NNEIGHBORS] = { 1, 1, 1, 0, -1, -1, -1, 0 }; /*8 neighbor*/
  int    i, j, k, x, y, n, lower, count;  /* counters */
  double  dx, dy;  
  double  celev;  /*celev =center elevation, Delev = the difference of elevation */
  double  neighbor_elev[NNEIGHBORS], neighbor_twi[NNEIGHBORS], temp_slope[NNEIGHBORS];
  double  length_diagonal;
  double  **tanbeta, **tanbeta_pixel;
  double  **contour_length, **Delev, **AveDelev;
  double  **wetnessindex, **mask;  
  ITEM    *OrderedCellsDEM;
  ITEM    *OrderedCellsTWI;
  int     Norow, VICrow;

  long time_begin,time_end;
  float elapsed_time; 
  float totalVeg = 0;
  float uplandVeg = 0;
  float wetlandVeg= 0;
  float waterVeg= 0;

  int VICcolumn = 5;  

  double **VIC;

  /*-------------- allocate memory------------*/
  /* mask */
  mask = Memoryalloc(columns, rows);

  /* wetnessindex */
  wetnessindex = Memoryalloc(columns, rows);

  /* tanbeta */
  tanbeta = Memoryalloc(columns, rows);

  /* tanbeta_pixel */
  tanbeta_pixel = Memoryalloc(columns, rows);

  /*contour_length */
  contour_length  = Memoryalloc(columns, rows);

  /*Delev */
  Delev = Memoryalloc(columns, rows);

  /* AveDelev */
  AveDelev = Memoryalloc(columns, rows);

  VIC = Memoryalloc(rows*columns, VICcolumn);


  /******  exclude the nodata  *********/
   /* This was already done in main. */
   Norow = 0;
   for(i=0; i<rows;i++) {
     for(j=0; j<columns; j++) {
       if(dem[i][j] == nodata)     {
	 Norow++;
       }}}
   
   VICrow = rows*columns - Norow; 
   //  fprintf(stderr, "Active cells = %d\n", VICrow);

   /*----------------------------------------------- */
   /*          Start calculation                     */
   /* Set 1d array to put elevation value for ranking*/
   /* This is not really needed since flow accumulation */
   /*   was already calculated, but it doesn't hurt. */
   /*------------------------------------------------*/

   count = (int)(rows*columns - Norow);   /*the size of 1d array */

   if(count==0)    /* There is no data in this cell*/
     { 
       fprintf(stderr, "ERROR: No data in current cell: %s. \n", gridno );
       exit(0);
     }

   /** Allocate memory **/
   if(!(OrderedCellsDEM=(ITEM*) calloc(count,sizeof(ITEM)))) 
     { 
       printf("Cannot allocate memory to first record: OrderedCellsDEM\n");
       exit(1); 
     } 
   if(!(OrderedCellsTWI=(ITEM*) calloc(count,sizeof(ITEM)))) 
     { 
       printf("Cannot allocate memory to first record: OrderedCellsTWI\n");
       exit(1); 
     } 

   /* Go through each row and column,and assign the elevation = dem[row][column] */
   count =0;
   for(i=0; i<rows;i++) {
     for(j=0; j<columns; j++)
       {
	 if(dem[i][j] != nodata) {
	   OrderedCellsDEM[count].Rank = dem[i][j];
	   OrderedCellsDEM[count].y = i;
	   OrderedCellsDEM[count].x = j;
	   count++;  
	 }
       }}

   /* Sort OrderedCellsfine/dems into ascending order (from low to high) */
   quick(OrderedCellsDEM, count);
   
   /* -----------------------------------------------*/
   /* Loop through all cells in descending order(from high to low) of elevation */
   /* -----------------------------------------------*/ 
   for (k = count-1; k >-1; k--) 
     { 
       y = (int)OrderedCellsDEM[k].y;/* y is rows*/
       x = (int)OrderedCellsDEM[k].x;/* x is columns*/	    
       dx = deltax;  /* meter to km */
       dy = deltay;  /* meter to km */   
       length_diagonal = sqrt((pow(dx, 2)) + (pow(dy, 2)));
      
       /* fill neighbor array*/
       for (n = 0; n < NNEIGHBORS; n++) 
	{
	  int xn = x + xneighbor[n]; /* calculate the x-axis of the neighbor cell */ 
	  int yn = y + yneighbor[n]; /* calculate the y-axis of the neighbor cell */
	  
	  /*Initialize neighbor_elev */
	  neighbor_elev[n] = (double) OUTSIDEBASIN;
	  
	  /*check to see if xn and yn are with in dem boundries*/
	  if(xn>=0 && yn>=0 && xn<columns && yn<rows)
	    {
	      neighbor_elev[n] = ((dem[yn][xn]!=nodata) ?   dem[yn][xn] :(double) OUTSIDEBASIN);
	    }
	}
      
       celev = dem[y][x]; /* the elevation of the center cell */
       lower = 0;         /* determine landscape position     */
       for (n = 0; n < NNEIGHBORS; n++)
	 {
	   if(neighbor_elev[n] == OUTSIDEBASIN) 
	     { /* If the neighbor_elev[n] still equal OUTSIDEBASIN,that means 
		  that it doesn't initialize  */ 
	       neighbor_elev[n] = nodata; 
	     }
	   
	   /* Calculating tanbeta as tanbeta * length of cell boundary between 
	      the cell of interest and downsloping neighbor. */
	   if(neighbor_elev[n] < celev && neighbor_elev[n] != nodata )
	     {
	       if(n==0 || n==2 || n==4 || n==6)
		 {
		   temp_slope[n] = (celev - neighbor_elev[n])/length_diagonal; /*slope               */
		   contour_length[y][x] += 0.2*dx+0.2*dy;                      /*contour length      */
		   tanbeta[y][x] += temp_slope[n]*(0.2*dx+0.2*dy);             /*tan beta            */
		 }
	       else if(n==1||n==5)
		 {
		   temp_slope[n] = (celev - neighbor_elev[n])/dy; /*slope               */
		   contour_length[y][x] += 0.6*dx ;
		   tanbeta[y][x] += temp_slope[n]*0.6*dx;
		 }
	       else if (n==3||n==7)
		 {
		   temp_slope[n] = (celev - neighbor_elev[n])/dx; /*slope               */
		   contour_length[y][x] += 0.6*dy;
		   tanbeta[y][x] += temp_slope[n]*0.6*dy;
		 }
	       /* Count how many neighbors are lower than current pixel. */
	       lower++;
	     }
	 }/* end for (n = 0; n < NNEIGHBORS; n++)*/
     
       /* if this is a flat area(slope doesn't change), then tanbeta = sum of 
	  (0.5 * vertical delta of elevation data)/ horizontal distance between 
	  centers of neighboring grid cells  --------------------------------------*/
       if (lower == 0) 
	 { 
	   tanbeta[y][x] = (4.*((0.5 * VERTRES)/length_diagonal) + 
			    (2.0*((0.5 * VERTRES)/dx)) + (2.0*((0.5 * VERTRES)/dy)))/NNEIGHBORS ;
	   tanbeta_pixel[y][x]= (4.*((0.5 * VERTRES)/length_diagonal) + 
			    (2.0*((0.5 * VERTRES)/dx)) + (2.0*((0.5 * VERTRES)/dy)))/NNEIGHBORS ;
	   contour_length[y][x] = 2.*dx + 2.*dy;
	}            
       else {
	 /* Calculate weighted average tanbeta at end of loop through pixel neighbors. */
	 tanbeta_pixel[y][x] =  tanbeta[y][x]/contour_length[y][x];
	 contour_length[y][x] /= (double)lower;
       }
     
       	 /* Add in an extra safety check. */ 
       if(tanbeta_pixel[y][x] <  (4.*((0.5 * VERTRES)/length_diagonal) + 
			    (2.0*((0.5 * VERTRES)/dx)) + 
				  (2.0*((0.5 * VERTRES)/dy)))/NNEIGHBORS ) {

       tanbeta_pixel[y][x] = (4.*((0.5 * VERTRES)/length_diagonal) + (2.0*((0.5 * VERTRES)/dx)) + (2.0*((0.5 * VERTRES)/dy)))/NNEIGHBORS; 

       }

	 /* Calculate general topographic index TI = A/(C.L.* tan B) */
       wetnessindex[y][x] = (double)(flowacc[y][x])/(contour_length[y][x]*tanbeta_pixel[y][x]);

       if (wetnessindex[y][x] >= WATERTHRESH)
	 {
	   waterVeg++;
	 }
       else if (wetnessindex[y][x] >= WETLANDTHRESH && wetnessindex[y][x] < WATERTHRESH)
	 {
	   wetlandVeg++;
	 } /* end wetland vegetation */
       
       if (wetnessindex[y][x] != nodata)
	 {
	   totalVeg++;
	 }
     } /* end  for (k = 0; k < count-1; k++) { */
  
  
   uplandVeg = (totalVeg-waterVeg-wetlandVeg)/totalVeg;
   wetlandVeg /= totalVeg;
   waterVeg /= totalVeg;  

   
   /* Add a new loop to find elevation difference relative to wetness index difference. */
     for (k = count-1; k >-1; k--) 
     { 
       y = (int)OrderedCellsDEM[k].y;/* y is rows*/
       x = (int)OrderedCellsDEM[k].x;/* x is columns*/	    
      
       Delev[y][x] = 0.0;
       lower = 0;         /* determine landscape position     */

       /* fill neighbor array*/
       for (n = 0; n < NNEIGHBORS; n++) 
	 {
	  int xn = x + xneighbor[n]; /* calculate the x-axis of the neighbor cell */ 
	  int yn = y + yneighbor[n]; /* calculate the y-axis of the neighbor cell */
	  
	  /*Initialize neighbor_elev */
	  neighbor_elev[n] = nodata;
	  neighbor_twi[n] = nodata;
	  
	  /*check to see if xn and yn are with in dem boundries*/
	  if(xn>=0 && yn>=0 && xn<columns && yn<rows)
	    {
	      neighbor_elev[n] = ((dem[yn][xn]!=nodata) ?   dem[yn][xn] :nodata);
	      neighbor_twi[n] = ((wetnessindex[yn][xn]!=nodata) ?   wetnessindex[yn][xn] :nodata);
	    }
	  
	  if(neighbor_elev[n] < dem[y][x] && neighbor_elev[n] != nodata  && neighbor_twi[n] > wetnessindex[y][x] )
	    {
	      if(n==0 || n==2 || n==4 || n==6)
		Delev[y][x] += (dem[y][x] - neighbor_elev[n])/length_diagonal;
	      else if(n==1||n==5)
		Delev[y][x] += (dem[y][x] - neighbor_elev[n])/dy;
	      else
		Delev[y][x] += (dem[y][x] - neighbor_elev[n])/dx;
	      //fprintf(stderr, "Delev= %lf %lf %lf\n", (dem[y][x] - neighbor_elev[n]), tanbeta_pixel[y][x],  (wetnessindex[y][x]));
	      lower++;
	    }
	 }/* end for (n = 0; n < NNEIGHBORS; n++)*/
	 
       if(lower==0) {
	 AveDelev[y][x] = 0;
       }
       else {
	 /* Calculate average elevation change between center pixel and all lower neighbors. */
	 AveDelev[y][x] = Delev[y][x]/((double)lower); 
       }

       
     } /* end  for (k = 0; k < count-1; k++)  */

     
  /* ----------------------------------------------- */
  /* Rank the wetness index order for wetland cells only. */
  /* ----------------------------------------------- */
  
   count = (int)(rows*columns - Norow);   /*the size of 1d array */
   for(i=0; i<count;i++) 
      {
	      OrderedCellsTWI[count].Rank = 0.0;
	      OrderedCellsTWI[count].y = 0;
	      OrderedCellsTWI[count].x = 0;
      }

  if(wetlandVeg > 0.0) {
    count =0;
    for(i=0; i<rows;i++) 
      {
	for(j=0; j<columns; j++)
	  {
	    if (wetnessindex[i][j] >= WETLANDTHRESH && wetnessindex[i][j] < WATERTHRESH) {
	      OrderedCellsTWI[count].Rank = wetnessindex[i][j];
	      OrderedCellsTWI[count].y = i;
	      OrderedCellsTWI[count].x = j;

	      OrderedCellsDEM[count].Rank = dem[i][j];
	      OrderedCellsDEM[count].y = i;
	      OrderedCellsDEM[count].x = j;
	      count++;
	    }
	  }
      }
 
  
    /* Sort OrderedCellsfine/wetnessindex into ascending order 
       (from low to high) */
    quick(OrderedCellsTWI, count);
    quick(OrderedCellsDEM, count);

    for (k =0; k<count; k++)
      { 
	/* Assign values to VIC array in descending TWI order (from high to low.) */
	y = OrderedCellsTWI[count-1-k].y;
	x = OrderedCellsTWI[count-1-k].x;
 
	VIC[0][k]= flowacc[y][x];
	VIC[1][k]= wetnessindex[y][x];
	VIC[2][k]= tanbeta_pixel[y][x];
	VIC[3][k]= AveDelev[y][x];

	y = OrderedCellsDEM[k].y;
	x = OrderedCellsDEM[k].x;
	VIC[4][k]= dem[y][x];

		
      }
  }

  //  time_begin = times(&tt);

  VICcalculation(VIC , count, VICcolumn, gridno, wetlandVeg, waterVeg, totalVeg, deltax, deltay, option, 2.0*VIC[1][0]/WATERTHRESH);

  //  time_end = times(&uu);
  // elapsed_time= (float)(time_end-time_begin)/HZ ;
  

  /*This is used to free the memory that have been allocated*/ 
  for(i=0; i< VICcolumn-1; i++)
    {
      free(VIC[i]);     
   }
  free(VIC); 
  for(i=0; i<rows; i++)  
    { 
      free(mask[i]);
      free(wetnessindex[i]);
      free(tanbeta[i]);
      free(tanbeta_pixel[i]);
      free(contour_length[i]);
      free(Delev[i]);
      free(AveDelev[i]);
    }  
  free(mask);
  free(wetnessindex); 
  free(tanbeta);
  free(tanbeta_pixel);
  free(contour_length);
  free(Delev);
  free(AveDelev);
  //fprintf(stdout, " here here here2 elapsed_time =%f  %d %d %d %f\n", elapsed_time, i, VICcolumn, count, VIC[3][100]); 	
  //  free(OrderedCellsfine);


  return;
}/* END wetness FUNCTION*/


/* ----------------------  
  Allocate the memory 
 ------------------------*/
double **Memoryalloc(int columns, int rows)
{
  int i;
  double **arr2;
  /*-------------- allocate memory------------*/
  if(!(arr2 = (double**) calloc(rows,sizeof(double*))))
    { printf("Cannot allocate memory to first record: arr2\n");
      exit(8); 
    }
  for(i=0; i<rows;i++)
      if(!(arr2[i] = (double*) calloc(columns,sizeof(double)))) 
	{ printf("Cannot allocate memory to first record: arr2\n");
	  exit(8); 
	}

  return arr2;
}


/*-----------------------------------------------------------------
  Quick Sort Function: this subroutine starts the quick sort
 ------------------------------------------------------------------*/
void quick(ITEM *item, int count)
{
  qs(item,0,count-1);
  return;
}

/*----------------------------------------------------------------
 this is the quick sort subroutine - it returns the values in
 an array from high to low.
 -----------------------------------------------------------------*/
void qs(ITEM *item, int left,  int right)  
{
  register int i,j;
  ITEM x,y;
  
  i=left;
  j=right;
  x=item[(left+right)/2];
  
  do {
    while(item[i].Rank < x.Rank && i<right) i++;
    while(x.Rank < item[j].Rank && j>left) j--;
    
    if (i<=j) 
      {
	y=item[i];
	item[i]=item[j];
	item[j]=y;
	i++;
	j--;
      }
  } while (i<=j);
  
  if(left<j) qs(item,left,j);
  if(i<right) qs(item,i,right); 

  return;
}







/*****************************************************************************/
/*       VIC calculation                                                     */
/*****************************************************************************/
void VICcalculation(double **VIC, int n, int m, char gridno[], float wetlandVeg, float waterVeg, int totalVeg, double deltax, double deltay, char option[], double ElevRange)
{ 
  /* n is count of wetland cells, and m is # of column in VIC */
  int i, j;
  double **WetlandBin; /* arrays */
  int counter, cnt;
  int ROWS;
  double Sum_Area;
  double sumA, sumT, sumB, sumE; /* summary of frac_area, topoindex, tanbeta, elevation*/ 
  double *AREASUM, *DEMSUM, *SLOPE, *TWI, *BATHSUM;  /* area and dem array */
  double LakeArea, LakeDepth;
  float areacriteria = 0.091; /* max. fraction of grid cell area in each bin. */
  int WetBins, LakeBins;
  double Vsum, Bathsum;

  /*********************************************************************/
  /* Calculate wetland part of profile. */
  /*********************************************************************/

  //  fprintf(stderr, "ElevRange=%lf\n",ElevRange);

  /* Check if grid cell has lake component. */
  if(waterVeg > 0.0) {
    LakeBins = 4;
    
    /* Find lake depth as a function of lake area, based on regional regressions. */
    /* Lake area in square km to find lake depth in meters. */
    LakeArea = waterVeg*totalVeg*deltax*deltay/(1000.*1000.);
    if (LakeArea < 40.9375)
      LakeDepth  =  7.04 - 0.07 * LakeArea;
    else 
      LakeDepth =  4.17;
  }
  else {
    LakeBins = 0;
    LakeDepth = 0.0;
    LakeArea = 0.0;
  }

  WetBins = 0;  
  if(wetlandVeg > 0.0) {
    
    /* Find number of wetland bins for this cell. */ 
    WetBins = (int) ceil(wetlandVeg/areacriteria);
    if(WetBins < 5) {
      WetBins = 5;
    }
    areacriteria = wetlandVeg/WetBins;
  }

  if(strcmp(option,"SEA")==0 ) 
    ROWS=5;
  else
    ROWS=2;

  /* Allocate WetlandBin array */
  if(!(WetlandBin = (double**) calloc(ROWS,sizeof(double*))))
    {
      fprintf(stderr, "Cannot allocate memory to first record: C5\n");
      exit(8); 
    }
  for(i=0; i<ROWS;i++)
    {
      if(!(WetlandBin[i] = (double*) calloc(WetBins,sizeof(double)))) {
	fprintf(stderr, "Cannot allocate memory to first record: C5\n");
	exit(8); }
    }

  counter = 0;  /* Tracks which wetland bin we are in. */
    cnt = 0; 
    for (i = 0; i < n ; i++ )
      {
	WetlandBin[0][counter] += 1;
	
	if(strcmp(option,"SEA")==0 ) {
	  WetlandBin[1][counter] += VIC[1][i];
	  WetlandBin[2][counter] += VIC[2][i];
	  WetlandBin[3][counter] += VIC[3][i];
	}
	WetlandBin[ROWS-1][counter] = VIC[4][i];
	cnt++;
      
	/* If areacriteria% of the wetland has been summed. */
	if ( WetlandBin[0][counter]/totalVeg >= areacriteria || i == n-1 )
	  {
	    WetlandBin[0][counter] /= totalVeg; /* Area Fractions */

	    if(strcmp(option,"SEA")==0 ) {
	      for(j=1; j<ROWS-1; j++) 
		WetlandBin[j][counter] /=cnt;
	    }
	    cnt = 0; 
	    //	    fprintf(stderr, "Bath elev=%lf\n", WetlandBin[ROWS-1][counter]);
	    counter++;
	  }
      }

  /* Allocate the arrays for AREASUM and DEMSUM */
  if(!(AREASUM=(double*) calloc(WetBins + LakeBins + 1,sizeof(double)))) 
    {
      printf("Cannot allocate memory to first record: AREASUM\n");
      exit(1); 
    }   
  if(!(DEMSUM=(double*) calloc(WetBins + LakeBins + 1,sizeof(double)))) 
    {
      printf("Cannot allocate memory to first record: DEMSUM\n");
      exit(1); 
    } 
 if(!(BATHSUM=(double*) calloc(WetBins + LakeBins + 1,sizeof(double)))) 
    {
      printf("Cannot allocate memory to first record: DEMSUM\n");
      exit(1); 
    } 

  if(strcmp(option,"SEA")==0 ) 
    {
      if(!(SLOPE=(double*) calloc(WetBins + LakeBins + 1,sizeof(double)))) 
	{
	  printf("Cannot allocate memory to first record: AREASUM\n");
	  exit(1); 
	}   
      if(!(TWI=(double*) calloc(WetBins + LakeBins + 1,sizeof(double)))) 
	{
	  printf("Cannot allocate memory to first record: DEMSUM\n");
	  exit(1); 
	}       
    }

  AREASUM[0] = DEMSUM[0] = 0.0;
  if(strcmp(option,"SEA")==0 ) 
     SLOPE[0] = TWI[0] = 0.0;

  for(i=1; i<= LakeBins; i++) {
    DEMSUM[i] = DEMSUM[i-1] + (LakeDepth/LakeBins);
    BATHSUM[i] = BATHSUM[i-1] + (LakeDepth/LakeBins);

    AREASUM[i] = waterVeg*sqrt(DEMSUM[i]/LakeDepth); 

    if(strcmp(option,"SEA")==0 ) 
      {
	SLOPE[i] = VIC[2][0];
	TWI[i] = VIC[1][0];
      }
  }

  if(strcmp(option,"SEA")==0 ) {
    for(i=LakeBins+1; i<= WetBins+LakeBins; i++) {
      	TWI[i] = WetlandBin[1][i-(LakeBins+1)];
    }
  }

  /* Do calculations for wetland portion of the profile. */
 for(i=LakeBins+1; i<= WetBins+LakeBins; i++) {
  
    AREASUM[i] = AREASUM[i-1] + (WetlandBin[0][i-(LakeBins+1)]); 
    
    if(i>1)
    BATHSUM[i] = (WetlandBin[ROWS-1][i-(LakeBins+1)])- VIC[4][0] + BATHSUM[LakeBins]; /* Subtract minimum elevation. */
    else
    BATHSUM[i] = (WetlandBin[ROWS-1][i-(LakeBins+1)])- VIC[4][0]; /* Subtract minimum elevation. */

    if(strcmp(option,"SEA")==0 ) 
      {
	SLOPE[i] = WetlandBin[2][i-(LakeBins+1)];

	if(i>1) {
	  //	  DEMSUM[i] = DEMSUM[i-1] + WetlandBin[3][i-(LakeBins+1)];
	  DEMSUM[i] = BATHSUM[LakeBins] + ElevRange*(VIC[1][0] - WetlandBin[1][i-(LakeBins+1)])/(VIC[1][0]-WetlandBin[1][WetBins-1]);
	}	
	else
      	  DEMSUM[i] = ElevRange;
	
      }
    else
      DEMSUM[i] = BATHSUM[i];
 }

 if(strcmp(option,"SEA")==0 ) 
    {
      Vsum=Bathsum=0.0;
      for(i=LakeBins+1; i<= WetBins+LakeBins; i++) {
	/* calculate the accumulated fractional area and accumulated elevation */
 
	if(i> 1) {
	  Bathsum+= AREASUM[i-1]*(BATHSUM[i]-BATHSUM[i-1]) + 
	    0.5*(AREASUM[i]-AREASUM[i-1])*(BATHSUM[i]-BATHSUM[i-1]);
	  Vsum+= AREASUM[i-1]*(DEMSUM[i]-DEMSUM[i-1]) + 
	    0.5*(AREASUM[i]-AREASUM[i-1])*(DEMSUM[i]-DEMSUM[i-1]);
	}
	else 
	  {
	    Bathsum = 0.5*AREASUM[i]*(BATHSUM[i]);
	    Vsum = 0.5*AREASUM[i]*(DEMSUM[i]);
	  }
      }

      //    for(i=LakeBins+1; i<= WetBins+LakeBins; i++) 
      // DEMSUM[i] *= (Bathsum/Vsum);
      }

 
  if (AREASUM[WetBins+LakeBins]- (waterVeg + wetlandVeg) > 1e-5)
    {
      fprintf(stderr, "Total wetland fraction does not match: %e %lf %lf \n",AREASUM[WetBins+LakeBins] - (waterVeg + wetlandVeg), AREASUM[WetBins+LakeBins], wetlandVeg );
      exit(0);
    }    

  if(strcmp(option,"SEA")==0 ) {
    //    fprintf(stderr, "Output option is SEA.\n");
 
  /* Write the output file */
  /* print the VIC lake parameter's gridcellno, lakeidx, nodes, runoff depth, wfrac, depthin, rpercent */ 

  if(LakeBins+WetBins == 0) {
     fprintf(stdout, "%s 0 %d %.3lf 0.01 %.3lf 1.0\n", gridno, 1, 0.0, 0.0); 
     fprintf(stdout, "0.0 0.0 0.0 0.0\n"); 
  }
  else {
    fprintf(stdout, "%s 0 %d %.3lf 0.01 %.3lf 1.0\n", gridno, LakeBins+WetBins, LakeDepth+.01, LakeDepth+.01); 

    /* print in the VIC lake parameter's order decreasing order */
    for (i=WetBins+LakeBins; i > 0; i--)
      {
	fprintf(stdout, "%.3lf %.5lf %.1lf %.4lf ", DEMSUM[i], AREASUM[i], TWI[i], SLOPE[i]);  /* accum-area, accum-DEM, Wetness_index, slop*/ 
      }
    fprintf(stdout, "\n");
  }
  }
  else if(strcmp(option,"LAKE")==0) {
    // fprintf(stderr, "Output option is original lake parameter file.\n");
    
    /* Write the output file */
    /* print the VIC lake parameter's gridcellno, lakeidx, nodes, runoff depth, wfrac, depthin, rpercent */ 

    if(LakeBins+WetBins == 0) {
      fprintf(stdout, "%s 1 %d %.3lf 0.01 %.3lf 1.0\n", gridno, 1, 0.0, 0.0); 
      fprintf(stdout, "0.0 0.0\n"); 
    }
    else {
      fprintf(stdout, "%s 1 %d %.3lf 0.01 %.3lf 1.0\n", gridno, LakeBins+WetBins, LakeDepth+.01, LakeDepth+.01); 

      /* print in the VIC lake parameter's order decreasing order */
      for (i=WetBins+LakeBins; i > 0; i--)
      {
	fprintf(stdout, "%.3lf %.5lf ", DEMSUM[i], AREASUM[i]);  /* accum-area, accum-DEM, Wetness_index, slop*/ 
      }
      fprintf(stdout, "\n");
    }
  }
  else
    fprintf(stderr, "Output option is not recognized.\n");

  /* free memory */
  free(AREASUM);
  free(DEMSUM);
  if(strcmp(option,"SEA")==0 ) {
    free(SLOPE);
    free(TWI);
  }
  return;
} //end of VICcalculation function

/* ------------------------------------------------------------------------
 * Function: least-squares.c and correlation
 * This program computes a linear model for a set of given data.
 *
 * PROBLEM DESCRIPTION:
 *  The method of least squares is a standard technique used to find
 *  the equation of a straight line from a set of data. Equation for a
 *  straight line is given by 
 *	 y = mx + b
 *  where m is the slope of the line and b is the y-intercept.
 *
 *  Given a set of n points {(x1,y1), x2,y2),...,xn,yn)}, let
 *      SUMx = x1 + x2 + ... + xn
 *      SUMy = y1 + y2 + ... + yn
 *      SUMxy = x1*y1 + x2*y2 + ... + xn*yn
 *      SUMxx = x1*x1 + x2*x2 + ... + xn*xn
 *
 *  The slope and y-intercept for the least-squares line can be 
 *  calculated using the following equations:
 *        slope (m) = ( n*SUMxy -SUMx*SUMy ) / ( n*SUMxx - SUMx*SUMx ) 
 *  y-intercept (b) = ( SUMy - slope*SUMx ) / n
 *  R  = ( n*SUMxy - SUMx*SUMy ) / sqrt(( n*SUMxx - SUMx*SUMx)*(n*SUMyy - SUMy*SUMy));
 *
 * AUTHOR: Dora Abdullah (Fortran version, 11/96)
 * REVISED: RYL (converted to C, 12/11/96)
 * ADAPTED: CMC (add correlation calculation)
 * ---------------------------------------------------------------------- */
double correlation (double *x, double *y, int n)
{
  double SUMx, SUMy, SUMxy, SUMxx, SUMyy, SUMres, res, slope, y_intercept, y_estimate, cor;
  int i;
   
  SUMx = 0; 
  SUMy = 0; 
  SUMxy = 0; 
  SUMxx = 0;
  SUMyy = 0;

  for (i=0; i<n; i++) 
    {
      SUMx = SUMx + x[i];
      SUMy = SUMy + y[i];
      SUMxy = SUMxy + x[i]*y[i];
      SUMxx = SUMxx + x[i]*x[i];
      SUMyy = SUMyy + y[i]*y[i];
    }
  slope = ( n*SUMxy -  SUMx*SUMy ) / ( n*SUMxx - SUMx*SUMx );
  y_intercept = ( SUMy - slope*SUMx ) / n;
  cor =   ( n*SUMxy - SUMx*SUMy ) / sqrt(( n*SUMxx - SUMx*SUMx)*(n*SUMyy - SUMy*SUMy));
  //printf ("\n The linear equation that best fits the given data:\n");
  //printf (" y = %6.2lfx + %6.2lf\n", slope, y_intercept);
  //printf ("--------------------------------------------------\n");
  //printf ("   Original (x,y)     Estimated y     Residual\n");
      
  SUMres = 0;
  for (i=0; i<n ; i++) 
    {
      y_estimate = slope*x[i] + y_intercept;
      res = y[i] - y_estimate;
      SUMres = SUMres + res*res;
      //printf ("(%6.2lf %6.2lf) %6.2lf %6.2lf\n", x[i], y[i], y_estimate, res);
    }
  //printf("--------------------------------------------------\n");
  //printf("Residual sum = %6.2lf  correlation = %6.2lf \n", SUMres, cor);

  return (cor);
}









/***************************************************************************/
/*                     Fill increment                                     */
/* Creates the global filled dem matrix (topo) and flow accumulation (flow) */
/* size_x = columns, size_y = rows [i][j] rows:columns
/**************************************************************************/
void fillin(double **dem, int lattice_size_x, int lattice_size_y, double deltax, double deltay, double nodata)
{
  int i,j,t,*topovecind;
  double *topovec;

  setupgridneighbors(lattice_size_x, lattice_size_y); /* The neighbor setting */

  topo=matrix(1,lattice_size_x,1,lattice_size_y);
  topovec=vector(1,lattice_size_x*lattice_size_y);
  topovecind=ivector(1,lattice_size_x*lattice_size_y);
  flow=matrix(1,lattice_size_x,1,lattice_size_y);
  flow1=matrix(1,lattice_size_x,1,lattice_size_y);
  flow2=matrix(1,lattice_size_x,1,lattice_size_y);
  flow3=matrix(1,lattice_size_x,1,lattice_size_y);
  flow4=matrix(1,lattice_size_x,1,lattice_size_y);
  flow5=matrix(1,lattice_size_x,1,lattice_size_y);
  flow6=matrix(1,lattice_size_x,1,lattice_size_y);
  flow7=matrix(1,lattice_size_x,1,lattice_size_y);
  flow8=matrix(1,lattice_size_x,1,lattice_size_y);

  /* Indexing conventions are different than we typically use. */
  /* topo begins indexing at 1 not zero and topo[col][row] vs. dem[row][col] */

  for (j=1;j<=lattice_size_y;j++) {
    for (i=1;i<=lattice_size_x;i++)
      {
	topo[i][j] = dem[j-1][i-1];
        flow[i][j]= deltax*deltay;
      } }

  for (j=1;j<=lattice_size_y;j++) {
    for (i=1;i<=lattice_size_x;i++)
      {

	fillinpitsandflats(i,j,lattice_size_x, lattice_size_y, nodata);
      } }

  //  fprintf(stderr, "Done with fill...\n");
    
  for (j=1; j<=lattice_size_y; j++){
    for (i=1; i<=lattice_size_x; i++){
      topovec[(j-1)*lattice_size_x+i]=topo[i][j];
    }}
  
  indexx(lattice_size_x*lattice_size_y,topovec,topovecind);
  t=lattice_size_x*lattice_size_y+1;

  while (t>1)
    {t--;
      i=(topovecind[t])%lattice_size_x;
      if (i==0) i=lattice_size_x;
      j=(topovecind[t])/lattice_size_x+1;
      if (i==lattice_size_x) j--;
      mfdflowroute(i,j, nodata);
    }

} /* End of fillin() */


int *ivector(long nl,long nh)
{ /* allocate an int vector with subscript range v[nl..nh] */
        int *v;

        v=(int *)malloc((unsigned int) ((nh-nl+1+NR_END)*sizeof(int)));
        return v-nl+NR_END;
}

double *vector(long nl, long nh)
{ /* allocate a double vector with subscript range v[nl..nh] */
        double *v;

        v=(double *)malloc((unsigned) ((nh-nl+1+NR_END)*sizeof(double)));
        return v-nl+NR_END;
}

void free_ivector(int *v, long nl, long nh)
{ /* free an int vector allocated with ivector() */
        free((FREE_ARG) (v+nl-NR_END));
}

void free_vector(double *v, long nl, long nh)
{ /* free an int vector allocated with ivector() */
        free((FREE_ARG) (v+nl-NR_END));
}

double **matrix(int nrl,int nrh,int ncl,int nch)
{  /* allocate a double matrix with subscript range m[nrl..nrh][ncl..nch] */
  int i, nrow=nrh-nrl+1,ncol=nch-ncl+1;
  double **m;

    /*allocate pointers to rows */
    m=(double **) malloc((unsigned) (nrow+1)*sizeof(double*));
    m+=1;
    m -= nrl;
    
    m[nrl]=(double *) malloc((unsigned)((nrow*ncol+1)*sizeof(double)));
    m[nrl] += 1;
    m[nrl] -= ncl;

   /*allocate rows and set pointers to them */
    for(i=nrl+1;i<=nrh;i++) {
      m[i]= m[i]=m[i-1]+ncol;
    }
    /* return pointer to array of pointers to rows */
    return m;
}



void indexx(int n,double arr[], int indx[])
{
        unsigned long i,indxt,ir=n,itemp,j,k,l=1;
        int jstack=0,*istack;
        double a;

        istack=ivector(1,NSTACK);
        for (j=1;j<=n;j++) indx[j]=j;
        for (;;) {
                if (ir-l < M) {
                        for (j=l+1;j<=ir;j++) {
                                indxt=indx[j];
                                a=arr[indxt];
                                for (i=j-1;i>=1;i--) {
                                        if (arr[indx[i]] <= a) break;
                                        indx[i+1]=indx[i];
                                }
                                indx[i+1]=indxt;
                        }
                        if (jstack == 0) break;
                        ir=istack[jstack--];
                        l=istack[jstack--];
                } else {
                        k=(l+ir) >> 1;
                        SWAP(indx[k],indx[l+1]);
                        if (arr[indx[l+1]] > arr[indx[ir]]) {
                                SWAP(indx[l+1],indx[ir])
                        }
                        if (arr[indx[l]] > arr[indx[ir]]) {
                                SWAP(indx[l],indx[ir])
                        }
                        if (arr[indx[l+1]] > arr[indx[l]]) {
                                SWAP(indx[l+1],indx[l])
                        }
                        i=l+1;
                        j=ir;
                        indxt=indx[l];
                        a=arr[indxt];
                        for (;;) {
                                do i++; while (arr[indx[i]] < a);
                                do j--; while (arr[indx[j]] > a);
                                if (j < i) break;
                                SWAP(indx[i],indx[j])
                        }
                        indx[l]=indx[j];
                        indx[j]=indxt;
                        jstack += 2;
                        if (ir-i+1 >= j-l) {
                                istack[jstack]=ir;
                                istack[jstack-1]=i;
                                ir=j-1;
                        } else {
                                istack[jstack]=j-1;
                                istack[jstack-1]=l;
                                l=i;
                        }
                }
        }
        free_ivector(istack,1,NSTACK);
}
#undef M
#undef NSTACK
#undef SWAP


void setupgridneighbors(int lattice_size_x, int lattice_size_y)
{    int i,j;

     idown=ivector(1,lattice_size_x);
     iup=ivector(1,lattice_size_x);
     jup=ivector(1,lattice_size_y);
     jdown=ivector(1,lattice_size_y);
     
     for (i=1;i<=lattice_size_x;i++)
      {
	idown[i]=i-1;
	iup[i]=i+1;
      }
     idown[1]=1;

     iup[lattice_size_x]=lattice_size_x;

     for (j=1;j<=lattice_size_y;j++)
      {
	jdown[j]=j-1;
	jup[j]=j+1;
      }
     jdown[1]=1;
     jup[lattice_size_y]=lattice_size_y;
}

void fillinpitsandflats(int i,int j, int lattice_size_x, int lattice_size_y, double nodata)
{    double min;
     

     if (topo[i][j] != nodata)   min=topo[i][j]; 
     if (topo[iup[i]][j] < min && topo[iup[i]][j] != nodata ) min=topo[iup[i]][j];
     if (topo[idown[i]][j]<min && topo[idown[i]][j] != nodata) min=topo[idown[i]][j];
     if (topo[i][jup[j]]<min && topo[i][jup[j]]!= nodata) min=topo[i][jup[j]];
     if (topo[i][jdown[j]]<min && topo[i][jdown[j]] != nodata) min=topo[i][jdown[j]];
     if (topo[iup[i]][jup[j]]<min && topo[iup[i]][jup[j]] != nodata) min=topo[iup[i]][jup[j]];
     if (topo[idown[i]][jup[j]]<min && topo[idown[i]][jup[j]] != nodata) min=topo[idown[i]][jup[j]];
     if (topo[idown[i]][jdown[j]]<min && topo[idown[i]][jdown[j]] != nodata) min=topo[idown[i]][jdown[j]];
     if (topo[iup[i]][jdown[j]]<min && topo[iup[i]][jdown[j]] != nodata) min=topo[iup[i]][jdown[j]];

     if ((topo[i][j] <= min)&& (topo[i][j]!=nodata)&&(i>1)&&(j>1)&&(i<lattice_size_x)&&(j<lattice_size_y))
      {
	topo[i][j]=min+fillincrement;
	fillinpitsandflats(i,j, lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(iup[i],j, lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(idown[i],j, lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(i,jup[j], lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(i,jdown[j], lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(iup[i],jup[j], lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(idown[i],jup[j], lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(idown[i],jdown[j], lattice_size_x, lattice_size_y, nodata);
	fillinpitsandflats(iup[i],jdown[j], lattice_size_x, lattice_size_y, nodata);
      }
    
}

void mfdflowroute(int i,int j, double nodata)
{ 
  double tot;
 
  if(topo[i][j] == nodata)
    flow1[i][j]=flow2[i][j]=flow3[i][j]=flow4[i][j]=flow5[i][j]=flow6[i][j]=flow7[i][j]=flow8[i][j]=0.0;
  else {
     tot=0.;
     if (topo[i][j]>topo[iup[i]][j] && topo[iup[i]][j]!= nodata) 
      tot+=pow(topo[i][j]-topo[iup[i]][j],1.1);
     if (topo[i][j]>topo[idown[i]][j] && topo[idown[i]][j]!= nodata) 
      tot+=pow(topo[i][j]-topo[idown[i]][j],1.1);
     if (topo[i][j]>topo[i][jup[j]] && topo[i][jup[j]]!= nodata) 
      tot+=pow(topo[i][j]-topo[i][jup[j]],1.1);
     if (topo[i][j]>topo[i][jdown[j]] && topo[i][jdown[j]]!= nodata) 
      tot+=pow(topo[i][j]-topo[i][jdown[j]],1.1);
     if (topo[i][j]>topo[iup[i]][jup[j]] && topo[iup[i]][jup[j]]!= nodata) 
      tot+=pow((topo[i][j]-topo[iup[i]][jup[j]])*oneoversqrt2,1.1);
     if (topo[i][j]>topo[iup[i]][jdown[j]] && topo[iup[i]][jdown[j]]!= nodata) 
      tot+=pow((topo[i][j]-topo[iup[i]][jdown[j]])*oneoversqrt2,1.1);
     if (topo[i][j]>topo[idown[i]][jup[j]] && topo[idown[i]][jup[j]]!= nodata) 
      tot+=pow((topo[i][j]-topo[idown[i]][jup[j]])*oneoversqrt2,1.1);
     if (topo[i][j]>topo[idown[i]][jdown[j]] && topo[idown[i]][jdown[j]]!= nodata) 
      tot+=pow((topo[i][j]-topo[idown[i]][jdown[j]])*oneoversqrt2,1.1);
    
     if (topo[i][j]>topo[iup[i]][j] && topo[iup[i]][j]!= nodata) 
       flow1[i][j]=pow(topo[i][j]-topo[iup[i]][j],1.1)/tot; 
     else flow1[i][j]=0;

     if (topo[i][j]>topo[idown[i]][j] && topo[idown[i]][j]!= nodata) 
      flow2[i][j]=pow(topo[i][j]-topo[idown[i]][j],1.1)/tot; 
     else flow2[i][j]=0;
     
     if (topo[i][j]>topo[i][jup[j]] && topo[i][jup[j]]!= nodata) 
      flow3[i][j]=pow(topo[i][j]-topo[i][jup[j]],1.1)/tot; 
     else flow3[i][j]=0;
     
     if (topo[i][j]>topo[i][jdown[j]] && topo[i][jdown[j]]!= nodata) 
       flow4[i][j]=pow(topo[i][j]-topo[i][jdown[j]],1.1)/tot; 
     else flow4[i][j]=0;
     
     if (topo[i][j]>topo[iup[i]][jup[j]] && topo[iup[i]][jup[j]]!= nodata) 
       flow5[i][j]=pow((topo[i][j]-topo[iup[i]][jup[j]])*oneoversqrt2,1.1)/tot;
     else flow5[i][j]=0;
     
     if (topo[i][j]>topo[iup[i]][jdown[j]] && topo[iup[i]][jdown[j]]!= nodata) 
       flow6[i][j]=pow((topo[i][j]-topo[iup[i]][jdown[j]])*oneoversqrt2,1.1)/tot;
     else flow6[i][j]=0;
     
     if (topo[i][j]>topo[idown[i]][jup[j]] && topo[idown[i]][jup[j]]!= nodata) 
       flow7[i][j]=pow((topo[i][j]-topo[idown[i]][jup[j]])*oneoversqrt2,1.1)/tot;
     else flow7[i][j]=0;
     
     if (topo[i][j]>topo[idown[i]][jdown[j]] && topo[idown[i]][jdown[j]]!= nodata) 
       flow8[i][j]=pow((topo[i][j]-topo[idown[i]][jdown[j]])*oneoversqrt2,1.1)/tot;
     else flow8[i][j]=0;
  }
     
     flow[iup[i]][j]+=flow[i][j]*flow1[i][j];
     flow[idown[i]][j]+=flow[i][j]*flow2[i][j];
     flow[i][jup[j]]+=flow[i][j]*flow3[i][j];
     flow[i][jdown[j]]+=flow[i][j]*flow4[i][j];
     flow[iup[i]][jup[j]]+=flow[i][j]*flow5[i][j];
     flow[iup[i]][jdown[j]]+=flow[i][j]*flow6[i][j];
     flow[idown[i]][jup[j]]+=flow[i][j]*flow7[i][j];
     flow[idown[i]][jdown[j]]+=flow[i][j]*flow8[i][j];
}


#ifndef _E_RADIUS
#define E_RADIUS 6371.0         /* average radius of the earth */
#endif

#ifndef _PI
#define PI 3.1415
#endif

double get_dist(double lat1, double long1, double lat2, double long2)
{
  double theta1;
  double phi1;
  double theta2;
  double phi2;
  double dtor;
  double term1;
  double term2;
  double term3;
  double temp;
  double dist;

  dtor = 2.0*PI/360.0;
  theta1 = dtor*long1;
  phi1 = dtor*lat1;
  theta2 = dtor*long2;
  phi2 = dtor*lat2;
  term1 = cos(phi1)*cos(theta1)*cos(phi2)*cos(theta2);
  term2 = cos(phi1)*sin(theta1)*cos(phi2)*sin(theta2);
  term3 = sin(phi1)*sin(phi2);
  temp = term1+term2+term3;
  temp = (double) (1.0 < temp) ? 1.0 : temp;
  dist = E_RADIUS*acos(temp);

  return dist;
}  

#undef E_RADIUS
#undef PI