/*******************************************************************************
 cgp.cpp: CGP
 Copyright (C) 2008 Brno University of Technology,
                    Faculty of Information Technology
 Author(s): Zdenek Vasicek <vasicek AT fit.vutbr.cz>

 LICENSE TERMS

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.
 3. All advertising materials mentioning features or use of this software
    or firmware must display the following acknowledgement:

      This product includes software developed by the University of
      Technology, Faculty of Information Technology, Brno and its
      contributors.

 4. Neither the name of the Company nor the names of its contributors
    may be used to endorse or promote products derived from this
    software without specific prior written permission.

 This software or firmware is provided ``as is'', and any express or implied
 warranties, including, but not limited to, the implied warranties of
 merchantability and fitness for a particular purpose are disclaimed.
 In no event shall the company or contributors be liable for any
 direct, indirect, incidental, special, exemplary, or consequential
 damages (including, but not limited to, procurement of substitute
 goods or services; loss of use, data, or profits; or business
 interruption) however caused and on any theory of liability, whether
 in contract, strict liability, or tort (including negligence or
 otherwise) arising in any way out of the use of this software, even
 if advised of the possibility of such damage.
*******************************************************************************/

#include <algorithm>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string>
#include <cstring>
#include <unistd.h>
#include "cgp.h"

typedef int *chromozom;                //dynamicke pole int, velikost dana m*n*(vstupu bloku+vystupu bloku) + vystupu komb
chromozom *populace[POPULACE_MAX];   //pole ukazatelu na chromozomy jedincu populace
int fitt[POPULACE_MAX];              //fitness jedincu populace
int uzitobloku(int *p_chrom);

int bestfit, bestfit_idx;   //nejlepsi fittnes, index jedince v populaci
int bestblk;

int *vystupy;               //pole vystupnich hodnot pro vyhodnocovani fce
int *pouzite;               //pole, kde kazda polozka odpovida bloku a urcuje zda se jedna o pouzity blok

int param_m = PARAM_M;            //pocet sloupcu
int param_n = PARAM_N;            //pocet radku
int param_in = PARAM_IN;          //pocet vstupu komb. obvodu
int param_out = PARAM_OUT;        //pocet vystupu komb. obvodu
int param_populace = POPULACE_MAX;//pocet jedincu populace
int block_in = 2;             //pocet vstupu  jednoho bloku (neni impl pro zmenu)
int l_back = L_BACK;              // 1 (pouze predchozi sloupec)  .. param_m (maximalni mozny rozsah);

int param_fitev;  //pocet pruchodu pro ohodnoceni jednoho chromozomu, vznikne jako (pocet vstupnich dat/(pocet vstupu+pocet vystupu))

int param_generaci; //pocet kroku evoluce
int data[DATASIZE]; //trenovaci data - vstupni hodnoty + k nim prislusejici spravne vystupni hodnoty
int sizesloupec = param_n*(block_in+1); //pocet polozek ktery zabira sloupec v chromozomu
int outputidx   = param_m*sizesloupec; //index v poli chromozomu, kde zacinaji vystupy
int maxidx_out  = param_n*param_m + param_in; //max. index pouzitelny jako vstup  pro vystupy
int maxfitness  = 0; //max. hodnota fitness

int fitpop, maxfitpop; //fitness populace

typedef struct { //struktura obsahujici mozne hodnoty vstupnich poli chromozomu pro urcity sloupec
    int pocet;   //pouziva se pri generovani noveho cisla pri mutaci
    int *hodnoty;
} sl_rndval;

sl_rndval **sloupce_val;  //predpocitane mozne hodnoty vstupu pro jednotlive sloupce
#define ARRSIZE PARAM_M*PARAM_N    //velikost pole = pocet bloku

#define LOOKUPTABSIZE 256
unsigned char lookupbit_tab[LOOKUPTABSIZE]; //LookUp pro rychle zjisteni poctu nastavenych bitu na 0 v 8bit cisle

#define copy_chromozome(from,to) (chromozom *) memcpy(to, from, (outputidx + param_out)*sizeof(int));

#define FITNESS_CALLCNT (POPULACE_MAX + PARAM_GENERATIONS*POPULACE_MAX) //pocet volani funkce fitness

//-----------------------------------------------------------------------
//Vypis chromozomu
//=======================================================================
//p_chrom ukazatel na chromozom
//-----------------------------------------------------------------------
void print_chrom(FILE *fout, chromozom p_chrom) {
  fprintf(fout, "{%d,%d, %d,%d, %d,%d,%d}", param_in, param_out, param_m, param_n, block_in, l_back, uzitobloku(p_chrom));
  for(int i=0; i<outputidx; i++) {
     if (i % 3 == 0) fprintf(fout,"([%d]",(i/3)+param_in);
     fprintf(fout,"%d", *p_chrom++);
     ((i+1) % 3 == 0) ? fprintf(fout,")") : fprintf(fout,",");
   }
  fprintf(fout,"(");
  for(int i=outputidx; i<outputidx+param_out; i++) {
     if (i > outputidx) fprintf(fout,",");
     fprintf(fout,"%d", *p_chrom++);
  }
  fprintf(fout,")");
  fprintf(fout,"\n");
}

void print_xls(FILE *xlsfil) {
  fprintf(xlsfil, "%d\t%d\t%d\t%d\t\t",param_generaci,bestfit,fitpop,bestblk);
  for (int i=0; i < param_populace;  i++)
      fprintf(xlsfil, "%d\t",fitt[i]);
  fprintf(xlsfil, "\n");
}

//-----------------------------------------------------------------------
//POCET POUZITYCH BLOKU
//=======================================================================
//p_chrom ukazatel na chromozom,jenz se ma ohodnotit
//-----------------------------------------------------------------------
int uzitobloku(chromozom p_chrom) {
    int i,j, in,idx, poc = 0;
    int *p_pom;
    memset(pouzite, 0, maxidx_out*sizeof(int));

    //oznacit jako pouzite bloky napojene na vystupy
    p_pom = p_chrom + outputidx;
    for (i=0; i < param_out; i++) {
        in = *p_pom++;
        pouzite[in] = 1;
    }

    //pruchod od vystupu ke vstupum
    p_pom = p_chrom + outputidx - 1;
    idx = maxidx_out-1;
    for (i=param_m; i > 0; i--) {
        for (j=param_n; j > 0; j--,idx--) {
            p_pom--; //fce
            if (pouzite[idx] == 1) { //pokud je blok pouzit, oznacit jako pouzite i bloky, na ktere je napojen
               in = *p_pom--; //in2
               pouzite[in] = 1;
               in = *p_pom--; //in1
               pouzite[in] = 1;
               poc++;
            } else {
               p_pom -= block_in; //posun na predchozi blok
            }
        }
    }

    return poc;
}


//-----------------------------------------------------------------------
//POCET POUZITYCH sloupcu - tedy teoreticky spozdeni
//=======================================================================
//p_chrom ukazatel na chromozom,jenz se ma ohodnotit
//-----------------------------------------------------------------------
int uzitosloupcu(chromozom p_chrom) {
    int i,j, in,idx, poc = 0;
    int *p_pom;
    memset(pouzite, 0, maxidx_out*sizeof(int));

    //oznacit jako pouzite bloky napojene na vystupy
    p_pom = p_chrom + outputidx;
    for (i=0; i < param_out; i++) {
        in = *p_pom++;
        pouzite[in] = 1;
    }

    //pruchod od vystupu ke vstupum
    p_pom = p_chrom + outputidx - 1;
    idx = maxidx_out-1;
    for (i=param_m; i > 0; i--) {
        for (j=param_n; j > 0; j--,idx--) {
            p_pom--; //fce
            if (pouzite[idx] > 0) { //pokud je blok pouzit, oznacit jako pouzite i bloky, na ktere je napojen
               in = *p_pom--; //in2
               if (pouzite[idx]+1 > pouzite[in])
			      pouzite[in] = pouzite[idx]+1;
               in = *p_pom--; //in1
               pouzite[in] = 1;
               if (pouzite[idx]+1 > pouzite[in])
			      pouzite[in] = pouzite[idx]+1;
               poc++;
            } else {
               p_pom -= block_in; //posun na predchozi blok
            }
        }
    }

    return *std::max_element(pouzite + param_in , pouzite + maxidx_out);
}



//-----------------------------------------------------------------------
//původní Fitness
//=======================================================================
//p_chrom ukazatel na chromozom,jenz se ma ohodnotit
//p_svystup ukazatel na pozadovane hodnoty vystupu
//p_vystup ukazatel na pole vstupnich a vystupnich hodnot bloku
//-----------------------------------------------------------------------
inline int fitness(chromozom p_chrom, int *p_svystup) {
    int in1,in2,fce;
    int i,j;
    int *p_vystup = vystupy;
    p_vystup += param_in; //posunuti az za hodnoty vstupu

    //Simulace obvodu pro dany stav vstupu
    //----------------------------------------------------------------------------
    for (i=0; i < param_m; i++) {  //vyhodnoceni funkce pro sloupec
        for (j=0; j < param_n; j++) { //vyhodnoceni funkce pro radky sloupce
            in1 = vystupy[*p_chrom++];
            in2 = vystupy[*p_chrom++];
            fce = *p_chrom++;
            switch (fce) {
              case 0: *p_vystup++ = in1; break;       //in1

              case 1: *p_vystup++ = in1 & in2; break; //and
              case 2: *p_vystup++ = in1 | in2; break; //or
              case 3: *p_vystup++ = in1 ^ in2; break; //xor

              case 4: *p_vystup++ = ~in1; break;  //not in1
              case 5: *p_vystup++ = ~in2; break;  //not in2

              case 6: *p_vystup++ = in1 & ~in2; break;
              case 7: *p_vystup++ = ~(in1 & in2); break;
              case 8: *p_vystup++ = ~(in1 | in2); break;
              default: ;
                 *p_vystup++ = 0xffffffff; //log 1
            }
        }
    }

    register int vysl;
    register int pocok = 0; //pocet shodnych bitu

    //Vyhodnoceni odezvy
    //----------------------------------------------------------------------------
    //pomoci 4 nahledu do lookup tabulky
    for (i=0; i < param_out; i++) {  
        vysl = (vystupy[*p_chrom++] ^ *p_svystup++);
        pocok += lookupbit_tab[vysl & 0xff]; //pocet 0 => pocet spravnych
        vysl = vysl >> 8;
        pocok += lookupbit_tab[vysl & 0xff];
        vysl = vysl >> 8;
        pocok += lookupbit_tab[vysl & 0xff];
        vysl = vysl >> 8;
        pocok += lookupbit_tab[vysl & 0xff];
    }

    /*
    //pomoci for cyklu
    vysl = ~(vystupy[*p_chrom++] ^ *p_svystup++); //bit 1 udava spravnou hodnotu
    for (j=0; j < 32; j++) {
        pocok += (vysl & 1);
        vysl = vysl >> 1;
    }
    */
    return pocok;
}



//-----------------------------------------------------------------------
//OHODNOCENI POPULACE
//=======================================================================
inline void ohodnoceni(int *vstup_komb, int minidx, int maxidx, int ignoreidx) {
    int fit;
    for (int l=0; l < param_fitev; l++) {
        //nakopirovani vstupnich dat na vstupy komb. site
        memcpy(vystupy, vstup_komb, param_in*sizeof(int));
        vstup_komb += param_in;

        //simulace obvodu vsech jedincu populace pro dane vstupy
        for (int i=minidx; i < maxidx; i++) {
            if (i == ignoreidx) continue;
            
            fit = fitness((int *) populace[i], vstup_komb);
            (l==0) ? fitt[i] = fit : fitt[i] += fit;
        }

        vstup_komb += param_out; //posun na dalsi vstupni kombinace
    }
}

//-----------------------------------------------------------------------
//MUTACE
//=======================================================================
//p_chrom ukazatel na chromozom, jenz se ma zmutovat
//-----------------------------------------------------------------------
inline void mutace(chromozom p_chrom) {
    int rnd;
    int genu = (rand()%MUTACE_MAX) + 1;     //pocet genu, ktere se budou mutovat
    for (int j = 0; j < genu; j++) {
        int i = rand() % (outputidx + param_out); //vyber indexu v chromozomu pro mutaci
        int sloupec = (int) (i / sizesloupec);
        rnd = rand();
        if (i < outputidx) { //mutace bloku
           if ((i % 3) < 2) { //mutace vstupu
              p_chrom[i] = sloupce_val[sloupec]->hodnoty[(rnd % (sloupce_val[sloupec]->pocet))];
           } else { //mutace fce
              p_chrom[i] = rnd % FUNCTIONS;
           }
        } else { //mutace vystupu
           p_chrom[i] = rnd % maxidx_out;
        }
    }
}

//-----------------------------------------------------------------------
// Původní MAIN
//-----------------------------------------------------------------------
int main_old(int argc, char* argv[])
{
    using namespace std;

    FILE *xlsfil;
    string logfname, logfname2;
    int rnd, fitn, blk;
    int *vstup_komb; //ukazatel na vstupni data
    bool log;
    int run_succ = 0;
    int i;
    int parentidx;
    
    logfname = "log";
    if ((argc == 2) && (argv[1] != "")) 
       logfname = string(argv[1]);
    
    vystupy = new int [maxidx_out+param_out];
    pouzite = new int [maxidx_out];

    init_data(data); //inicializace dat

	unsigned seed = (unsigned) time(NULL);
    srand(seed); //inicializace pseudonahodneho generator
	// printf("random seed = %d\n", seed);
	
    param_fitev = DATASIZE / (param_in+param_out); //Spocitani poctu pruchodu pro ohodnoceni
    maxfitness = param_fitev*param_out*32;         //Vypocet max. fitness
    
    for (int i=0; i < param_populace; i++) //alokace pameti pro chromozomy populace
        populace[i] = new chromozom [outputidx + param_out];
    
    //---------------------------------------------------------------------------
    // Vytvoreni LOOKUP tabulky pro rychle zjisteni poctu nenulovych bitu v bytu
    //---------------------------------------------------------------------------
    for (int i = 0; i < LOOKUPTABSIZE; i++) {
        int poc1 = 0;
        int zi = ~i;
        for (int j=0; j < 8; j++) {
            poc1 += (zi & 1);
            zi = zi >> 1;
        }
        lookupbit_tab[i] = poc1;
    }

    //-----------------------------------------------------------------------
    //Priprava pole moznych hodnot vstupu pro sloupec podle l-back a ostatnich parametru
    //-----------------------------------------------------------------------
    sloupce_val = new sl_rndval *[param_m];
    for (int i=0; i < param_m; i++) {
        sloupce_val[i] = new sl_rndval;

        int minidx = param_n*(i-l_back) + param_in;
        if (minidx < param_in) minidx = param_in; //vystupy bloku zacinaji od param_in do param_in+m*n
        int maxidx = i*param_n + param_in;

        sloupce_val[i]->pocet = param_in + maxidx - minidx;
        sloupce_val[i]->hodnoty = new int [sloupce_val[i]->pocet];

        int j=0;
        for (int k=0; k < param_in; k++,j++) //vlozeni indexu vstupu komb. obvodu
            sloupce_val[i]->hodnoty[j] = k;
        for (int k=minidx; k < maxidx; k++,j++) //vlozeni indexu moznych vstupu ze sousednich bloku vlevo
            sloupce_val[i]->hodnoty[j] = k;
    }

    //-----------------------------------------------------------------------
    // printf("LogName: %s  l-back: %d  popsize:%d\n", logfname.c_str(), l_back, param_populace);
    //-----------------------------------------------------------------------

    for (int run=0; run < PARAM_RUNS; run++) {
        time_t t;
        struct tm *tl;
        char fn[100];
    
        t = time(NULL);
        tl = localtime(&t);
    
        sprintf(fn, "_%d", run);
        logfname2 = logfname + string(fn);
        strcpy(fn, logfname2.c_str()); strcat(fn,".xls");
        xlsfil = fopen(fn,"wb");
        if (!xlsfil) {
           printf("Can't create file %s!\n",fn);
           return -1;
        }
        //Hlavicka do log. souboru
        fprintf(xlsfil, "Generation\tBestfitness\tPop. fitness\t#Blocks\t\t");
        for (int i=0; i < param_populace;  i++) fprintf(xlsfil,"chrom #%d\t",i);
        fprintf(xlsfil, "\n");
    /*
        printf("----------------------------------------------------------------\n");
        printf("Run: %d \t\t %s", run, asctime(tl));
        printf("----------------------------------------------------------------\n");
    // */
        //-----------------------------------------------------------------------
        //Vytvoreni pocatecni populace
        //-----------------------------------------------------------------------
        chromozom p_chrom;
        int sloupec;
        for (int i=0; i < param_populace; i++) {
            p_chrom = (chromozom) populace[i];
            for (int j=0; j < param_m*param_n; j++) {
                sloupec = (int)(j / param_n);
                // vstup 1
                *p_chrom++ = sloupce_val[sloupec]->hodnoty[(rand() % (sloupce_val[sloupec]->pocet))];
                // vstup 2
                *p_chrom++ = sloupce_val[sloupec]->hodnoty[(rand() % (sloupce_val[sloupec]->pocet))];
                // funkce
                rnd = rand() % FUNCTIONS;
                *p_chrom++ = rnd;
            }
            for (int j=outputidx; j < outputidx+param_out; j++)  //napojeni vystupu
                *p_chrom++ = rand() % maxidx_out;
        }

        //-----------------------------------------------------------------------
        //Ohodnoceni pocatecni populace
        //-----------------------------------------------------------------------
        bestfit = 0; bestfit_idx = -1;
        ohodnoceni(data /*vektor ocekavanych dat*/, 0, param_populace, -1);
        for (int i=0; i < param_populace; i++) { //nalezeni nejlepsiho jedince
            if (fitt[i] > bestfit) {
               bestfit = fitt[i];
               bestfit_idx = i;
            }
        }
    
        //bestfit_idx ukazuje na nejlepsi reseni v ramci pole jedincu "populace"
        //bestfit obsahuje fitness hodnotu prvku s indexem bestfit_idx

        if (bestfit_idx == -1) 
           return 0;
    
        //-----------------------------------------------------------------------
        // EVOLUCE
        //-----------------------------------------------------------------------
        param_generaci = 0;
        maxfitpop = 0;
        while (param_generaci++ < PARAM_GENERATIONS) {
            //-----------------------------------------------------------------------
            //Periodicky vypis chromozomu populace
            //-----------------------------------------------------------------------
            #ifdef PERIODIC_LOG
            if (param_generaci % PERIODICLOGG == 0) {
               printf("Generation: %d\n",param_generaci);
               for(int j=0; j<param_populace; j++) {
                  printf("{%d, %d}",fitt[j],uzitobloku((int *)populace[j]));
                  print_chrom(stdout,(chromozom)populace[j]);
               }
            }
            #endif

            //-----------------------------------------------------------------------
            //mutace nejlepsiho jedince populace (na param_populace mutantu)
            //-----------------------------------------------------------------------
            for (int i=0, midx = 0; i < param_populace;  i++, midx++) {
                if (bestfit_idx == i) continue;

                p_chrom = (int *) copy_chromozome(populace[bestfit_idx],populace[midx]);
                mutace(p_chrom);
            }

            //-----------------------------------------------------------------------
            //ohodnoceni populace
            //-----------------------------------------------------------------------
            ohodnoceni(data, 0, param_populace, bestfit_idx);
            parentidx = bestfit_idx;
            fitpop = 0;
            log = false;
            for (int i=0; i < param_populace; i++) { 
                fitpop += fitt[i];
                
                if (i == parentidx) continue; //preskocime rodice

                if (fitt[i] == maxfitness) {
                   //optimalizace na poc. bloku obvodu

                   blk = uzitobloku((chromozom) populace[i]);
                   if (blk <= bestblk) {

                      if (blk < bestblk) {
                         // printf("Generation:%-10d\t\tbestblk b:%d\r",param_generaci,blk);
                         log = true;
                      }

                      bestfit_idx = i;
                      bestfit = fitt[i];
                      bestblk = blk;
                   }
                } else if (fitt[i] >= bestfit) {
                   //nalezen lepsi nebo stejne dobry jedinec jako byl jeho rodic

                   if (fitt[i] > bestfit) {
                      // printf("Generation:%-10d\t\tFittness: %d/%d\r",param_generaci,fitt[i],maxfitness);
                      log = true;
                   }

                   bestfit_idx = i;
                   bestfit = fitt[i];
                   bestblk = ARRSIZE;
                }
            }
    
            //-----------------------------------------------------------------------
            // Vypis fitness populace do xls souboru pri zmene fitness populace/poctu bloku
            //-----------------------------------------------------------------------
            if ((fitpop > maxfitpop) || (log)) {
               print_xls(xlsfil);

               maxfitpop = fitpop;
               log = false;
            }
        }
        //-----------------------------------------------------------------------
        // Konec evoluce
        //-----------------------------------------------------------------------
        print_xls(xlsfil);
        fclose(xlsfil);
        /*
		printf("\nBest chromosome fitness: %d/%d\n",bestfit,maxfitness);
        // printf("Best chromosome: ");
        // print_chrom(stdout, (chromozom)populace[bestfit_idx]);
		printf("pocet využitých hradel: %d\n",uzitobloku((chromozom)populace[bestfit_idx]));
		printf("pocet využitých sloupců: %d\n",uzitosloupcu((chromozom)populace[bestfit_idx]));
		printf("cgp mřížka: %d*%d\n",param_m,param_n);
		// */
		printf("%d %d %d %d %d\n",bestfit,uzitobloku((chromozom)populace[bestfit_idx]),uzitosloupcu((chromozom)populace[bestfit_idx]), param_m, param_n);
        
		
		
        if (bestfit == maxfitness) {
            strcpy(fn, logfname2.c_str()); strcat(fn,".chr");
            FILE *chrfil = fopen(fn,"wb");
            fprintf(chrfil, POPIS);
            print_chrom(chrfil, (chromozom)populace[bestfit_idx]);
            fclose(chrfil);
        }

        if (bestfit == maxfitness) 
           run_succ++; 
    } //runs
	
    for (int i=param_populace-1; i >= 0; i--)
        delete[] populace[i];
    // printf("Successful runs: %d/%d (%5.1f%%)\n",run_succ, PARAM_RUNS, 100*run_succ/(float)PARAM_RUNS);
    return 0;
}


int main(int argc, char* argv[])
{
	for (int i=2; i < PARAM_M; i++) {
		for (int j=2; j < PARAM_N; j++) {

			param_m = i;
			param_n = j;
			l_back = param_m;
			sizesloupec = param_n*(block_in+1);
			outputidx   = param_m*sizesloupec;
			maxidx_out  = param_n*param_m + param_in;
			
			main_old(argc,argv);
		}
	}
}