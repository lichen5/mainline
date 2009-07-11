/*
 
 Copyright (C) 2007 Lucas K. Wagner
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License along
 with this program; if not, write to the Free Software Foundation, Inc.,
 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 
 */

#include "converter.h"
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "basis_writer.h"
#include "Pseudo_writer.h"
#include "wf_writer.h"

using namespace std;
enum coord_t { bohr, ang, scaled, fractional };

void read_lattice_vector(istream & is, vector <string> & currline, 
                         vector <vector <double> > & latvec);
void read_atoms(istream & is, vector <string> & currline, 
                         vector <Atom> & atoms,coord_t & coord_type);

template <typename T>
void read_mo_coefficients(istream & is, vector <string> & currline,
                          Slat_wf_writer & slwriter, vector <vector <T> > & moCoeff);

//reads in the basis for each type of atom
void read_basis(vector <Atom> & atoms, vector<Spline_basis_writer> & basis );
void read_psp(vector <Atom> & atoms, vector <Spline_pseudo_writer> & pseudo);

template <typename T>
void fix_basis_norm(vector <Atom> & atoms, vector <Spline_basis_writer> & basis,
                    vector < vector <T> > & moCoeff);

template <typename T> 
T safe_read(string& real, string& imaginary);

void orbital_split(string line, vector <string> & currline);

template <typename T>
int run_main(int argc, char **argv, int is_complex);
//###########################################################################


int main(int argc, char ** argv) { 
  if(argc < 2) {
    cout << "usage: siesta2qmc < -o basename > < -fold fold_dir nfold > < -use_complex > [siesta stdout]\n";
    cout << "  -o          use basename as the prefix for QWalk.  Defaults to qwalk\n";
    exit(1);
  }
  
  int is_complex = 0;
  int result = 0;

  for(int i=1; i< argc-1; ++i)  
    if(!strcmp(argv[i],"-use_complex"))
      is_complex = 1;
  
  if (is_complex)
    result = run_main<dcomplex>(argc, argv, 1);
  else
    result = run_main<doublevar>(argc, argv, 0);

  return result;
}

template <typename T>
int run_main(int argc, char **argv, int is_complex)
{
  string outputname="qwalk";
  int fold_dir=-1;
  int nfold=0;
  
  for(int i=1; i< argc-1; i++) { 
    if(!strcmp(argv[i],"-o") && i+1 < argc) { 
      outputname=argv[++i];
    }
    if(!strcmp(argv[i],"-fold") && i+2 < argc) { 
      fold_dir=atoi(argv[++i]);
      nfold=atoi(argv[++i]);
    }
  }
  string infilename=argv[argc-1];
  
  //------------------------------read in the variables from the output file
  vector <Atom> atoms;
  vector <vector < double> > latvec;
  Slat_wf_writer slwriter;
  
  //Default Magnification
  slwriter.magnification=10.0;
 
  vector <vector <T> > moCoeff;
  if (is_complex) 
    {
      slwriter.orbtype = "CORBITALS";
      slwriter.mo_matrix_type = "CCUTOFF_MO";

      //Default magnification proved to be very problematic for
      //cmplx wavefuction as is also noted in crystal2qmc
      //this is a quick fix for now, there is probably a better way
      slwriter.magnification *= 5.0;
    }
  else
    slwriter.mo_matrix_type="CUTOFF_MO";

  int spin_pol=0;
  vector <Spline_pseudo_writer> pseudo;
  
  
  ifstream is(infilename.c_str());
  if(!is) { 
    cout << "Couldn't open " << infilename << endl;
    exit(1);
  }
  string line;
  string space=" ";
  vector <string> currline;
  int netCharge=0;
  coord_t coord_type;
  while(getline(is,line)) { 
    currline.clear();
    split(line, space, currline);
    read_atoms(is,currline,atoms, coord_type);
    read_lattice_vector(is,currline,latvec);
    read_mo_coefficients(is, currline, slwriter, moCoeff);

    if(currline.size()> 6 && currline[2]=="spin" && currline[3]=="polarization") { 
      spin_pol=atoi(currline[6].c_str());
      double tmp=atof(currline[6].c_str());
      if(fabs(spin_pol-tmp) > 0.5) spin_pol+=1;
      assert(fabs(spin_pol-tmp) < 0.1);
      cout << "spin polarization " << spin_pol << endl;
    }
    if(currline.size() > 8 && currline[0]=="redata:" && currline[1]=="Net" && currline[2]=="charge") {
      cout << "getting charge on " << line << endl;
      netCharge=atoi(currline[7].c_str());
      double tmp=atof(currline[7].c_str());
      if(fabs(netCharge-tmp) > 0.5) spin_pol+=1;
      assert(fabs(netCharge-tmp) < 0.1);
      cout << "net charge " << netCharge << endl;
    }
  }
  is.close();
  slwriter.orbname=outputname+".orb";
  slwriter.basisname=outputname+".basis";
 
  cout << " coord_type " << coord_type << endl;
  if(coord_type==scaled || coord_type==fractional) { 
    cout << "rescaling " << endl;
    vector <Atom> tmpatoms=atoms;
    int natoms=atoms.size();
    for(int at=0; at < natoms; at++) { 
      for(int i=0; i< 3; i++) atoms[at].pos[i]=0.0;
      for(int i=0; i< 3; i++) { 
        for(int j=0; j< 3; j++) { 
          atoms[at].pos[i]+=tmpatoms[at].pos[j]*latvec[j][i];
        }
      }
    }
  }
 
  vector <Spline_basis_writer> basis;
  read_basis(atoms, basis);
  read_psp(atoms, pseudo);
  fix_basis_norm(atoms,basis, moCoeff);


  for(int i=0; i< nfold; i++) 
    { 
      fold_kpoint(slwriter, latvec,fold_dir,moCoeff,atoms);
    }
  
  int nelectrons=0;
  for(vector<Atom>::iterator at=atoms.begin(); 
      at != atoms.end(); at++) { 
    nelectrons+=at->charge;
  }
  nelectrons-=netCharge;
  cout << "nelectrons " << nelectrons << endl;
  assert((nelectrons-spin_pol)%2==0);
  slwriter.nup=int(nelectrons-spin_pol)/2+spin_pol;
  slwriter.ndown=int(nelectrons-spin_pol)/2;
  
  //---------------------------------------------
  //--Write out all the collected data
  //---------------------------------------------
  
  
  //---------------------Basis file
  ofstream os (slwriter.basisname.c_str());
  os.precision(15);
  for(vector<Spline_basis_writer>::iterator i=basis.begin();
      i!= basis.end(); i++) {
    os << "BASIS { \n";
    i->print_basis(os);
    os << "}\n\n";
  }
  os.close(); os.clear();
  
  //-----------------ORB file
  
  os.open(slwriter.orbname.c_str());
  //this is really redundant with gamess2qmc.cpp..should probably be refactored 
  vector < Center> centers;
  vector <int> nbasis;
  centers.resize(atoms.size());
  int natoms=atoms.size();
  nbasis.resize(natoms);
  for(int at=0; at < natoms; at++) {
    for(int i=0; i< 3; i++) centers[at].pos[i]=atoms[at].pos[i];
    centers[at].equiv_atom=at;
    centers[at].name=atoms[at].name;
    nbasis[at]=basis[atoms[at].basis].nfunc();
  }

  print_orbitals(os,centers,nbasis, moCoeff);

  os.close(); os.clear();
  
  //--------------------Slater determinant file
  
  string slateroutname=outputname+".slater";
  os.open(slateroutname.c_str());
  slwriter.print_wavefunction(os);
  os.close(); os.clear();
  
  
  //---------------------sys file
  //May want to add a command-line switch to print out a molecule
  string sysoutname=outputname+".sys";
  ofstream sysout(sysoutname.c_str());
  sysout << "SYSTEM { PERIODIC \n";
  sysout << "  NSPIN { " << slwriter.nup << "  "
    << slwriter.ndown << " } \n";
  for(int at=0; at <natoms; at++) {
    atoms[at].print_atom(sysout);
  }
  
  sysout << "LATTICEVEC { \n";
  double min_latsize=1e8;
  for(int i=0; i< 3; i++) { 
    double length=0;
    for(int j=0; j< 3; j++) {
      sysout << latvec[i][j] << " ";
      length+=latvec[i][j]*latvec[i][j];
    }
    sysout << endl;
    if(min_latsize > length) min_latsize=length;
  }
  sysout << "  }\n";
  //Here we assume that nothing has a larger cutoff than 10.0 bohr..is that a good guess?
  sysout << "  cutoff_divider " << sqrt(min_latsize)/10.0 << endl;
  sysout << "}\n\n\n";
  
  int npsp=pseudo.size();
  for(int psp=0; psp < npsp; psp++) {
    pseudo[psp].print_pseudo(sysout);
  }
  sysout.close();
  
  //--------------------------Jastrow 2 output

  double basis_cutoff=find_basis_cutoff(latvec);
  string jast2outname=outputname+".jast2";
  Jastrow2_wf_writer jast2writer;
  jast2writer.set_atoms(atoms);
  
  
  ofstream jast2out(jast2outname.c_str());
  print_std_jastrow2(jast2writer, jast2out, basis_cutoff);
  jast2out.close();
  
}

//###########################################################################

void read_atoms(istream & is, vector <string> & currline, 
                vector <Atom> & atoms, coord_t & coord_type) {
  string space=" ";
  if(currline.size() > 3 && currline[0]=="outcoor:" && currline[2]=="coordinates") { 
    atoms.clear(); //so we always take the last set of atoms.
    double fac=1;
    if(currline[3]=="(Bohr):") {
      fac=1;
      coord_type=bohr;
    }
    else if(currline[3]=="(Ang):") { 
      cout << "converting from angstrom" << endl;
      fac=1/.529177249;
      coord_type=ang;
    }
    else if(currline[3]=="(scaled):") { 
      cout << "scaled coordinates: WARNING: if you have non-orthogonal unit cells, the conversion may be incorrect.  Check atomic coordinates!!" << endl;
      fac=1.0;
      coord_type=scaled;
    }
    else if(currline[3]=="(fractional):") { 
      cout << "fractional coordinates" << endl;
      fac=1.0;
      coord_type=fractional;
    }
    else { 
      cout << "Don't understand format of the coordinates: " << currline[3] << endl;
      exit(1);
    }
    //cout << "reading in coordinates" << endl;
    string line;
    getline(is,line);
    currline.clear(); split(line, space, currline);
    while(currline.size() > 0) { 
      Atom tmpatom;
      tmpatom.name=currline[4];
      //This is the *label*, not the charge, so that will be changed later(in the psp reading)
      tmpatom.charge=atoi(currline[3].c_str()); 
      for(int i=0; i< 3; i++) { 
        tmpatom.pos[i]=atof(currline[i].c_str())*fac;
      }
      atoms.push_back(tmpatom);
      //tmpatom.print_atom(cout);
      getline(is,line);
      currline.clear(); split(line,space,currline);
      
    }
      
  }
}

//###########################################################################
void read_lattice_vector(istream & is, vector <string> & currline, 
                         vector <vector <double> > & latvec) {
  string space=" ";
  if(currline.size() > 3 && currline[0]=="outcell:" && currline[3]=="vectors") { 
    latvec.clear();
    double fac=1;
    if(currline[4]=="(Ang):") { 
      fac=1/.529177249;
    }
    else if(currline[4]!="(Bohr):") { 
      cout << "Don't understand units: " << currline[4] << endl;
    }
    //cout << "reading in lattice vectors " << endl;
    string line;
    latvec.resize(3);
    for(int i=0; i< 3; i++) { 
      latvec[i].resize(3);
      currline.clear();
      getline(is,line); split(line, space,currline);
      for(int j=0; j< 3; j++) { 
        latvec[i][j]=atof(currline[j].c_str())*fac;
      }
    }
  }
}

//###########################################################################
//This function reads the two strings from the siesta input file which give
//the real and complex coefficients, and safely places it into the desired orbital
//coefficient type, which will be either doublevar or dcomplex
template <typename T>
T safe_read(string& real, string& imaginary)
{
  T result;
  stringstream sstream(real + " " + imaginary);
  if (!(sstream >> result)) throw exception();
  return result;
}
//###########################################################################
//It turns out, that when there are imaginary coeff's that are both double,
//digit and negative, that the real and imaginary coeff's are not bridged by
//a space, but rather by the - sign.  Thus the more general split() function
//needs a modification
void orbital_split(string line, vector <string> & currline)
{
  string space = " ";
  split(line, space, currline);
  if (currline.size() < 7) //If last two were joined by a -...
    {
      int loc = currline[5].rfind("-", currline[5].size());
      string temp_1 = currline[5].substr(0, loc - 1);
      string temp_2 = currline[5].substr(loc, currline[5].size());
      currline[5] = temp_1;
      currline.push_back(temp_2);
    }
}


//###########################################################################
template <typename T>
void read_mo_coefficients(istream & is, vector <string> & currline,
                          Slat_wf_writer & slwriter, vector <vector <T> > & moCoeff) { 
  slwriter.use_global_centers=true;
  slwriter.write_centers=false;
  string space=" ";
  if(currline.size() > 3 && currline[0]=="writewave:" && currline[2]=="Functions") { 
    string line;
    getline(is, line); //empty line;
    getline(is, line); //k-points
    getline(is, line); //nspin
    currline.clear(); split(line, space, currline);
    //cout << line << endl;
    int nspin=atoi(currline[4].c_str());
    if(nspin==1) { 
      slwriter.calctype="RHF";
    }
    else if(nspin==2) { 
      slwriter.calctype="UHF";
    }
    else { cout << "error reading nspin " << endl; exit(1); }
    
    getline(is,line); //'Number of basis orbs
    currline.clear(); split(line,space, currline);
    int nbasis=atoi(currline[5].c_str());
    cout << "calc type " << slwriter.calctype << "  nbasis " << nbasis << endl;

    getline(is,line); 
    
    for(int spin=0; spin < nspin; spin++) { 
      getline(is, line); getline(is,line); //one blank line and *'s
      getline(is,line); //k-point line, ignore for now
      getline(is,line); //spin component
      getline(is,line); //Num. wavefunctions
      currline.clear(); split(line, space, currline);
      int norb=atoi(currline[3].c_str());
      //cout << "norb " << norb << endl;
      //Now start with reading the ****orbitals*** (KS wave functions)
      for(int orb_num=0; orb_num < norb; orb_num++) { 
        for(int i=0; i< 5; i++) getline(is,line); //blank, wf#, energy, ---'s, header
        vector <T> orb(nbasis);
        for(int b=0; b< nbasis; b++) { 
          getline(is, line);
          currline.clear(); 
	  orbital_split(line, currline);
	  orb[b] = safe_read<T>(currline[5],currline[6]);
        }
        moCoeff.push_back(orb);
        getline(is, line); //a line of --'s
      }
      if(spin==0 && nspin==2) { 
        slwriter.spin_dwn_start=moCoeff.size();
      }
    } //spin loop
    //cout << "total norbs " << moCoeff.size() << endl;

  }
  
  
  //does not fill nup, ndown, mo_matrix_type, orbname, or basisname in slwriter
}



//###########################################################################

void smooth_grid(vector <double> & r,
                 vector <double> & vals) { 
  vector <double> newvals;
  newvals.resize(vals.size());
  int n=vals.size();
  assert(r.size()==vals.size());
  for(int i=2; i< n-2; i++) { 
    newvals[i]=(vals[i-2]+2*vals[i-1]+3*vals[i]+2*vals[i+1]+vals[i+2])/9.0;
  }
  newvals[1]=(vals[0]+vals[1]+vals[2])/3.0;
  newvals[0]=vals[0];
  newvals[n-2]=(vals[n-3]+vals[n-2]+vals[n-1])/3.0;
  newvals[n-1]=vals[n-1];
  vals=newvals;
}


//Make a uniform grid out of a 
void make_uniform(vector <double> & r, 
                  vector <double> & vals,
                  vector <double> & ur,
                  vector <double> & uvals) { 
  const double spacing=0.05; //bohr..
  double cutoff=10.0; //mostly for convienence..we work in terms of npts below
                      //this can be set quite high, since QWalk will find where to cut it off anyway
                      //(and fairly agressively)
  int npts=int(cutoff/spacing);
  
  int interval=0;
  for(int i=0; i< npts; i++) { 
    double rad=i*spacing;
    double val=0.0;
    if(i==0)  { 
      val=vals[0];
    }
    else { 
      while(r[interval+1] < rad && interval < r.size()-1) interval++;
      double x=(rad-r[interval])/(r[interval+1]-r[interval]);
      //cout << "rad " << rad << " x " << x << " r1 " << r[interval] 
      //  << " r2 " << r[interval+1] << endl;
      val=(1-x)*vals[interval]+x*vals[interval+1];
    }
    ur.push_back(rad);
    uvals.push_back(val);
  }
  
}


//###########################################################################

void pad_spline(vector <double> & r, vector <double> & vals) { 
  double spacing=r[1]-r[0];
  double extent=r[r.size()-1];
  while(extent < 14.0) {  
    extent+=spacing;
    r.push_back(extent);
    vals.push_back(0.0);
  }
}


//Here we smoothly cut off the function instead of just chopping it off, 
//as Siesta seems happy to do.
void smooth_cutoff(vector <double> & r, 
                  vector <double> & vals,
                  double smooth=1.2) { 
  int n=r.size();
  double threshold=1e-8;
  double cut=r[n-1];
  for(int i=n-1; i> 0; i--) { 
    if(fabs(vals[i]) > threshold) { 
      cut=r[i];
      cout << "Found cutoff radius of " << cut << " at " << i <<  endl;
      break;
    }
  }
  
  double cutmin=cut-smooth;
  vector <double> newvals;
  newvals.resize(n);
  double a,b,c,d; //extrapolation parameters
  for(int j=0; j< n; j++) {
    if(r[j] > cutmin) {
      if(r[j] > cut) { //if we're beyond the cutoff completely
        newvals[j]=0;
      }
      else {  //if we're in the smooth cutoff region
        double zz=(r[j]-cutmin)/smooth;
        newvals[j] = vals[j]*(1-zz*zz*zz*(6*zz*zz-15*zz+10));
      }
      
        //better, but makes a small jump right at the cutoff
      //if(j+1 < n)
      //  newvals[j]=0.25*vals[j-1]+0.5*vals[j]+0.25*vals[j+1];
      if(j+2 < n) 
        newvals[j]=(vals[j-2]+2*vals[j-1]+3*vals[j]+2*vals[j+1]+vals[j+2])/9.0;
      else newvals[j]=0.0;
      
    }
    else newvals[j]=vals[j];
    
  }
/*
if(r[j] >= cutmin && r[j-1] < cutmin) { 
  double v, der, der2;
  v=vals[j];
  double diff=r[j+1]-r[j-1];
  der=(vals[j+1]-vals[j-1])/diff;
  der2=(vals[j+1]+vals[j-1])/(diff*diff);
  double cutmin3=cutmin*cutmin*cutmin;
  double cut3=cut*cut*cut;
  smooth=-smooth;
  cout << "der " << der << " der2 " << der2 << endl;
  a=(der2-2/smooth*(der-v/smooth))/(6.*cutmin-2/smooth*(3*cutmin*cutmin-(cutmin3-cut3)/smooth));
  b=1/smooth*(der-v/smooth-a*(3*cutmin*cutmin-(cutmin3-cut3)/smooth));
  c=(v-a*(cutmin3-cut3))/smooth-b*(cutmin+cut);
  d=-a*cut3-b*cut*cut-c*cut;
  cout << "a,b,c,d " << a << " " << b << " " << c <<"  "<< d << endl;
  newvals[j]=vals[j];
  cout <<"val " << vals[j] << " function " << a*r[j]*r[j]*r[j]+b*r[j]*r[j]+c*r[j]+d << endl;
}
else if(r[j] > cutmin && r[j] < cut) { 
  
  newvals[j]=a*r[j]*r[j]*r[j]+b*r[j]*r[j]+c*r[j]+d;
}
else if(r[j] > cut) newvals[j]=0.0;
    else newvals[j]=vals[j];
  }
  vals=newvals;
*/
}

//###########################################################################


void read_basis(vector <Atom> & atoms, vector<Spline_basis_writer> & basis ) {
  vector <string> unique_atoms;
  find_unique_atoms(atoms,unique_atoms);
  
  for(vector<string>::iterator nm=unique_atoms.begin();
      nm != unique_atoms.end(); nm++) { 
    Spline_basis_writer tmp_basis;
    tmp_basis.label=*nm;
    //do S orbitals..
    for(int el=1; el < 5; el++) { //search over possible l-values
      string prefix="ORB.S";
      append_number(prefix, el);
      prefix+=".";
      string postfix="."+*nm;
      int n=1;
      while(true) { 
        string file=prefix;
        append_number(file,n);
        file+=postfix;
        ifstream is(file.c_str());
        if(!is) break;
        cout << "found " << file << endl;
        switch(el) { 
          case 1:
            tmp_basis.types.push_back("S");
            break;
          case 2:
            tmp_basis.types.push_back("P_siesta");
            break;
          case 3:
            tmp_basis.types.push_back("5D_siesta");
            break;
          case 4:
            tmp_basis.types.push_back("7F_siesta");
            break;
          default:
            cout << "Don't support this l-value.  Bug the maintainer." << endl;
            exit(1);
        }
        //Read in the file..
        is.ignore(180,'\n'); is.ignore(180,'\n');
        double rad, val;
        vector <double> rads, vals;
        while(is >> rad && is >> val) {
          rads.push_back(rad); vals.push_back(val);
        }
        vector <double> urad, uval;
        smooth_grid(rads,vals);
        pad_spline(rads,vals);
        urad=rads; uval=vals;
        //make_uniform(rads,vals,urad,uval);
        //smooth_cutoff(urad,uval);
        tmp_basis.rad.push_back(urad);
        tmp_basis.vals.push_back(uval);
        n++;
      }
    }
    basis.push_back(tmp_basis);
  }
  
  //We only have one basis object per atom name type, so 
  //assigning the basis number is pretty easy.
  for(vector <Atom>::iterator at=atoms.begin(); at!= atoms.end(); 
      at++) { 
    int nbasis=basis.size();
    for(int i=0; i< nbasis; i++) { 
      if(basis[i].label==at->name) { 
        at->basis=i;
      }
    }
  }
}





//###########################################################################


void read_psp(vector <Atom> & atoms, vector <Spline_pseudo_writer> & pseudo) {
  vector <string> unique_atoms;
  //cout << "****Pseudopotential " << endl;
  string space=" ";
  find_unique_atoms(atoms,unique_atoms);
  for(vector<string>::iterator at=unique_atoms.begin();
      at != unique_atoms.end(); at++) { 
    Spline_pseudo_writer tmp_pseudo;
    string filename=*at+".psf";
    ifstream is(filename.c_str());
    if(!is) { 
      cout << "Couldn't open " << filename << endl;
      exit(1);
    }
    for(int i=0; i< 3; i++) is.ignore(180,'\n'); //first three lines are random bits..
    string line;
    getline(is, line);
    vector <string> spl;
    split(line, space, spl);
    //cout << *at << " : effective charge : " << spl[5] <<  " integerized " << atoi(spl[5].c_str()) << endl;
    int zeff=atoi(spl[5].c_str());
    for(vector<Atom>::iterator i=atoms.begin(); i!= atoms.end();
        i++) { 
      if(i->name==*at) i->charge=zeff;
    }
    int npoints=atoi(spl[2].c_str());
    int nl=atoi(spl[0].c_str());
    int nl_up=atoi(spl[1].c_str());
    if(nl_up > 0) { 
      cout << "Can't deal with spin-polarized pseudopotentials..sorry\n";
      exit(1);
    }
    //cout << "npoints " << npoints <<  " nl " << nl << endl;
    is.ignore(180,'\n'); //Radial grid..
    vector <double>  rad;
    for(int i=0; i< npoints; i++) { 
      double dum;
      is >> dum;
      rad.push_back(dum);
    }

    for(int l=0; l< nl; l++) { 
      vector <double> val;
      is.ignore(180,'\n');
      getline(is, line);
      //cout << line << endl;
      int currl; is >> currl;
      //cout << "l " << l << " "  << currl << endl;
      assert(l==currl);
      

      for(int i=0; i< npoints; i++) { 
        double dum; is >> dum; 
        val.push_back(dum);
        val[i]*= 0.5/rad[i];
      }
      vector <double> urad, uval;
      make_uniform(rad,val,urad,uval);
      
      //string outname="psp";
      //append_number(outname,l);
      //ofstream out(outname.c_str());
      //int n=urad.size();
      //for(int i=0; i< n; i++) {
      //  out << urad[i] << "  " << uval[i] << endl;
      //}
      
      tmp_pseudo.psp_pos.push_back(urad);
      tmp_pseudo.psp_val.push_back(uval);
      
    }
    tmp_pseudo.label=*at;
    int npts=tmp_pseudo.psp_val[0].size();
    for(int i=0; i< npts; i++) { 
      for(int l=0; l< nl-1; l++) { 
        tmp_pseudo.psp_val[l][i]-=tmp_pseudo.psp_val[nl-1][i];
      }
    }
    pseudo.push_back(tmp_pseudo);
  }
}


//###########################################################################

template <typename T>
void fix_basis_norm(vector <Atom> & atoms, 
                    vector <Spline_basis_writer> & basis,
                    vector < vector <T> > & moCoeff) {
  
  int nmo=moCoeff.size();
  int funcnum=0;
  
  //Looping over each symmetry in the basis attached to each atom
  for(vector<Atom>::iterator at=atoms.begin();
      at != atoms.end(); at++) { 
    vector<Spline_basis_writer>::iterator bas=basis.begin();
    //Assuming that there is just one basis per atom,
    //which should be correct..
    while(bas->label != at->name) bas++;
    
    for(vector<string>::iterator type=bas->types.begin();
        type!=bas->types.end(); type++) { 
      int L=0;
      if(*type=="S") L=0;
      else if(*type=="P_siesta") L=1;
      else if(*type=="5D_siesta") L=2;
      else if(*type=="7F_siesta") L=3;
      else { cout << "unknown type! " << *type << endl; exit(3); }
      double norm=(2.0*L+1)/(4*pi);
      for(int m=-L; m< L+1; m++) { 
        int absm=abs(m);
        double i=1;
        for(int j=L-absm+1; j < L+absm+1; j++) {
          i*=j;
        }
        double mnorm=norm/i;
        if(m!=0) mnorm*=2.;
        mnorm=sqrt(mnorm);
        if(L==1) { 
          switch(m) { 
            case -1:
              mnorm*=-1; break;
            case 0:
              mnorm*=1; break;
            case 1:
              mnorm*=-1; break;
          }
        }
        else if(L==2) { 
          switch(m) { 
            case -2:
              mnorm*=6;
              break;
            case -1:
              mnorm*=-3; break;
            case 0:
              mnorm*=0.5;break;
            case 1:
              mnorm*=-3; break;
            case 2:
              mnorm*=3; break;
            default:
              cout << "error in mnorm assignment" << endl; exit(3);
          }
        }
        else if(L==3) { 
          switch(m) { 
            case -2:
              mnorm*=2;
              break;
          }
        }
        
              
                
        //cout << "i " << i << "  mnorm " << mnorm << endl;

        for(int mo=0; mo < nmo; mo++) { 
          //cout << moCoeff[mo].size() << " " << funcnum << endl;
          if(moCoeff[mo].size() <= funcnum) { 
            cout << "You have less MO coefficients than atomic orbitals in the ORB files.  This could be from a remnant\n"
            "of a previous run.  The easiest solution is to rm ORB* and rerun the siesta calculation or try to guess\n"
            "which ORB files are extraneous\n";
          }
          assert(moCoeff[mo].size() > funcnum);
          moCoeff[mo][funcnum]*=mnorm;
        }
        funcnum++;
      }
    }
    
  }
  
  
}

