#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include "block.hpp"
#include "cartesian.hpp"
#include "fd.hpp"
#include "fields.hpp"
#include "friction.hpp"
#include "interface.hpp"
#include "load.hpp"
#include <mpi.h>

using namespace std;

friction::friction(const char* filename, const int ndim_in, const int mode_in, const int niface,
                   block**** blocks, const fields& f, const cartesian& cart, const fd_type& fd) : interface(filename, ndim_in, mode_in, niface, blocks, f, cart, fd) {
    // constructor initializes interface and then allocates memory for slip velocity and slip
    
    is_friction = true;
    
    // allocate memory for slip and slip rate arrays
    
    if (no_data) {return;}
    
    ux = new double [(ndim-1)*n_loc[0]*n_loc[1]];
    dux = new double [(ndim-1)*n_loc[0]*n_loc[1]];
    vx = new double [(ndim-1)*n_loc[0]*n_loc[1]];
    
    u = new double [n_loc[0]*n_loc[1]];
    du = new double [n_loc[0]*n_loc[1]];
    v = new double [n_loc[0]*n_loc[1]];
    
    // initialize slip and slip velocity
    
    for (int i=0; i<ndim-1; i++) {
        for (int j=0; j<n_loc[0]; j++) {
            for (int k=0; k<n_loc[1]; k++) {
                ux[i*n_loc[0]*n_loc[1]+j*n_loc[1]+k] = 0.;
                vx[i*n_loc[0]*n_loc[1]+j*n_loc[1]+k] = 0.;
            }
        }
    }
    
    for (int i=0; i<n_loc[0]; i++) {
        for (int j=0; j<n_loc[1]; j++) {
            u[i*n_loc[1]+j] = 0.;
            v[i*n_loc[1]+j] = 0.;
        }
    }
    
    // read loads from input file
    
    string* ltype;
    double* t0;
    double* x0;
    double* y0;
    double* dx;
    double* dy;
    double* sn;
    double* s2;
    double* s3;
    
    stringstream ss;
    
    ss << niface;
    
    string line;
    ifstream paramfile(filename, ifstream::in);
    if (paramfile.is_open()) {
        // scan to start of appropriate interface list
        while (getline(paramfile,line)) {
            if (line == "[fdfault.interface"+ss.str()+"]") {
                break;
            }
        }
        // scan to start of next friction list
        // note does not include interface number, so can specify multiple frictional
        // interfaces with the same input
        while (getline(paramfile,line)) {
            if (line == "[fdfault.friction]") {
                break;
            }
        }
        if (paramfile.eof()) {
            cerr << "Error reading interface "+ss.str()+" from input file\n";
            MPI_Abort(MPI_COMM_WORLD,-1);
        } else {
            // read interface variables
            paramfile >> nloads;
            ltype = new string [nloads];
            t0 = new double [nloads];
            x0 = new double [nloads];
            y0 = new double [nloads];
            dx = new double [nloads];
            dy = new double [nloads];
            sn = new double [nloads];
            s2 = new double [nloads];
            s3 = new double [nloads];
            for (int i=0; i<nloads; i++) {
                paramfile >> ltype[i];
                paramfile >> t0[i];
                paramfile >> x0[i];
                paramfile >> dx[i];
                paramfile >> y0[i];
                paramfile >> dy[i];
                paramfile >> sn[i];
                paramfile >> s2[i];
                paramfile >> s3[i];
            }
        }
    } else {
        cerr << "Error opening input file in interface.cpp\n";
        MPI_Abort(MPI_COMM_WORLD,-1);
    }
    paramfile.close();
    
    nloads = 2;
    
    loads = new load* [nloads];
    
    double x_2d[2], l_2d[2];
    
    switch (direction) {
        case 0:
            x_2d[0] = x[1];
            x_2d[1] = x[2];
            l_2d[0] = l[1];
            l_2d[1] = l[2];
            break;
        case 1:
            x_2d[0] = x[0];
            x_2d[1] = x[2];
            l_2d[0] = l[0];
            l_2d[1] = l[2];
            break;
        case 2:
            x_2d[0] = x[0];
            x_2d[1] = x[1];
            l_2d[0] = l[0];
            l_2d[1] = l[1];
    }
    
    for (int i=0; i<nloads; i++) {
        loads[i] = new load(ltype[i], t0[i], x0[i], dx[i], y0[i] , dy[i], sn[i], s2[i], s3[i], n, xm, xm_loc, x_2d, l_2d);
    }
    
    delete[] t0;
    delete[] x0;
    delete[] y0;
    delete[] dx;
    delete[] dy;
    delete[] sn;
    delete[] s2;
    delete[] s3;
    
}

friction::~friction() {
    // destructor to deallocate memory
    
    if (no_data) {return;}
    
    delete[] ux;
    delete[] dux;
    delete[] vx;
    
    delete[] u;
    delete[] du;
    delete[] v;
    
    for (int i=0; i<nloads; i++) {
        delete loads[i];
    }
    
    delete[] loads;

}

iffields friction::solve_interface(const boundfields b1, const boundfields b2, const int i, const int j, const double t) {
    // solves boundary conditions for a frictionless interface
    
    ifchar ifcp, ifchatp;
    
    ifcp.v1 = b1.v1;
    ifcp.v2 = b2.v1;
    ifcp.s1 = b1.s11;
    ifcp.s2 = b2.s11;
    
    ifchatp = solve_locked(ifcp,zp1,zp2);
    
    iffields iffin, iffout;
    
    iffin.v12 = b1.v2;
    iffin.v22 = b2.v2;
    iffin.v13 = b1.v3;
    iffin.v23 = b2.v3;
    iffin.s12 = b1.s12;
    iffin.s22 = b2.s12;
    iffin.s13 = b1.s13;
    iffin.s23 = b2.s13;
    
    iffout = solve_friction(iffin, ifchatp.s1, zs1, zs2, i, j, t);
    
    iffout.v11 = ifchatp.v1;
    iffout.v21 = ifchatp.v2;
    iffout.s11 = ifchatp.s1;
    iffout.s21 = ifchatp.s2;
    
    return iffout;
    
}

iffields friction::solve_friction(iffields iffin, double sn, const double z1, const double z2, const int i, const int j, const double t) {
    // solve friction law for shear tractions and slip velocities
    
    const double eta = z1*z2/(z1+z2);
    double phi, phi2, phi3, v2, v3;
    
    for (int k=0; k<nloads; k++) {
        sn += loads[k]->get_sn(i,j,t);
        iffin.s12 += loads[k]->get_s2(i,j,t);
        iffin.s22 += loads[k]->get_s2(i,j,t);
        iffin.s13 += loads[k]->get_s3(i,j,t);
        iffin.s23 += loads[k]->get_s3(i,j,t);
    }
    
    phi2 = eta*(iffin.s12/z1-iffin.v12+iffin.s22/z2+iffin.v22);
    phi3 = eta*(iffin.s13/z1-iffin.v13+iffin.s23/z2+iffin.v23);
    phi = sqrt(pow(phi2,2)+pow(phi3,2));
    
    boundchar b = solve_fs(phi, eta, sn, i, j);
    
    if (fabs(b.v) < 1.e-14 && fabs(b.s) < 1.e-14) {
        v2 = 0.;
        v3 = 0.;
    } else {
        v2 = b.v*phi2/(eta*b.v+b.s);
        v3 = b.v*phi3/(eta*b.v+b.s);
    }
    
    // set interface variables to hat variables
    
    v[i*n_loc[1]+j] = b.v;
    switch (ndim) {
        case 3:
            vx[0*n_loc[0]*n_loc[1]+i*n_loc[1]+j] = v2;
            vx[1*n_loc[0]*n_loc[1]+i*n_loc[1]+j] = v3;
            break;
        case 2:
            switch (mode) {
                case 2:
                    vx[0*n_loc[0]*n_loc[1]+i*n_loc[1]+j] = v2;
                    break;
                case 3:
                    vx[0*n_loc[0]*n_loc[1]+i*n_loc[1]+j] = v3;
            }
    }
    
    iffields iffout;
    
    iffout.s12 = phi2-eta*v2;
    iffout.s22 = iffout.s12;
    iffout.s13 = phi3-eta*v3;
    iffout.s23 = iffout.s13;
    iffout.v12 = (iffout.s12-iffin.s12)/z1+iffin.v12;
    iffout.v22 = (-iffout.s22+iffin.s22)/z2+iffin.v22;
    iffout.v13 = (iffout.s13-iffin.s13)/z1+iffin.v13;
    iffout.v23 = (-iffout.s23+iffin.s23)/z2+iffin.v23;
    
    
    for (int k=0; k<nloads; k++) {
        iffout.s12 -= loads[k]->get_s2(i,j,t);
        iffout.s22 -= loads[k]->get_s2(i,j,t);
        iffout.s13 -= loads[k]->get_s3(i,j,t);
        iffout.s23 -= loads[k]->get_s3(i,j,t);
    }
    
    return iffout;
    
}

boundchar friction::solve_fs(const double phi, const double eta, const double sn, const int i, const int j) {
    // solves friction law for slip velocity and strength
    // frictionless interface
    
    boundchar b;
    
    b.v = phi/eta;
    b.s = 0.;
    
    return b;
}

void friction::scale_df(const double A) {
    // scale df for state variables by rk constant A
    
    for (int i=0; i<ndim-1; i++) {
        for (int j=0; j<n_loc[0]; j++) {
            for (int k=0; k<n_loc[1]; k++) {
                dux[i*n_loc[0]*n_loc[1]+j*n_loc[1]+k] *= A;
            }
        }
    }
    
    for (int i=0; i<n_loc[0]; i++) {
        for (int j=0; j<n_loc[1]; j++) {
            du[i*n_loc[1]+j] *= A;
        }
    }
    
}

void friction::calc_df(const double dt) {
    // calculate df for state variables for rk time step
    
    
    int id;
    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    
    for (int i=0; i<ndim-1; i++) {
        for (int j=0; j<n_loc[0]; j++) {
            for (int k=0; k<n_loc[1]; k++) {
                dux[i*n_loc[0]*n_loc[1]+j*n_loc[1]+k] += dt*vx[i*n_loc[0]*n_loc[1]+j*n_loc[1]+k];
            }
        }
    }
    
    for (int i=0; i<n_loc[0]; i++) {
        for (int j=0; j<n_loc[1]; j++) {
            du[i*n_loc[1]+j] += dt*v[i*n_loc[1]+j];
        }
    }
    
}

void friction::update(const double B) {
    // updates state variables
    
    for (int i=0; i<ndim-1; i++) {
        for (int j=0; j<n_loc[0]; j++) {
            for (int k=0; k<n_loc[1]; k++) {
                ux[i*n_loc[0]*n_loc[1]+j*n_loc[1]+k] += B*dux[i*n_loc[0]*n_loc[1]+j*n_loc[1]+k];
            }
        }
    }
    
    for (int i=0; i<n_loc[0]; i++) {
        for (int j=0; j<n_loc[1]; j++) {
            u[i*n_loc[1]+j] += B*du[i*n_loc[1]+j];
        }
    }
    
}

void friction::write_fields() {
    // writes interface fields
    
    int id;
    
    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    
    stringstream ss;
    ss << id;
    
    fstream myFile;
    myFile.open (("data/i"+ss.str()+".dat").c_str(), ios::out | ios::binary);
    
    myFile.write((char*) ux, sizeof(double)*(ndim-1)*n_loc[0]*n_loc[1]);
    myFile.write((char*) vx, sizeof(double)*(ndim-1)*n_loc[0]*n_loc[1]);
    myFile.write((char*) u, sizeof(double)*n_loc[0]*n_loc[1]);
    myFile.write((char*) v, sizeof(double)*n_loc[0]*n_loc[1]);
    
    myFile.close();

}


