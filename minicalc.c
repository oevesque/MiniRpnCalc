/*
 * MiniCalc RPN - calculatrice RPN (notation polonaise inverse, style HP) en C pur, API Win32.
 * Aucune DLL a part celles de Windows : demarrage quasi instantane.
 *
 * ORGANISATION DU FICHIER (de haut en bas)
 *   1. Moteur RPN        : pile, saisie en cours, calculs (do_cmd). Aucune dependance a
 *                          Windows : on peut donc le tester en ligne de commande (voir "Test").
 *   2. Presse-papiers    : copier / coller un nombre (Windows uniquement).
 *   3. Clavier           : key_char() traduit un caractere tape en action sur le moteur.
 *   4. Interface Windows : fenetre, dessin de l'ecran, boutons, listes deroulantes,
 *                          sauvegarde de l'etat. Tout ce qui suit "Interface Windows" n'est
 *                          compile que hors mode TEST.
 *
 * VOCABULAIRE
 *   - "niveau 1" = sommet de la pile (st[n-1]), "niveau 2" = st[n-2], etc.
 *   - Une "commande" est une chaine ("SIN", "SWAP", "x^y"...) executee par do_cmd().
 *     Le clavier, les boutons et les listes deroulantes passent tous par do_cmd().
 *   - La saisie en cours (ent / elen) n'est pas encore sur la pile : commit() l'y pousse.
 *
 * POUR AJOUTER UNE FONCTION
 *   1. L'ecrire dans do_cmd() sous la forme   if (IS("NOM")) { ... return; }
 *      (une fonction a un seul argument peut utiliser la macro UN()).
 *   2. L'ajouter au tableau mitems[] (liste MENU) ou a stk_menu() (bouton STK), avec la
 *      chaine "NOM" comme commande. Pour une nouvelle colonne : mettre a jour colstart[]
 *      et la boucle de menu_dropdown().
 *   3. Si besoin : un raccourci clavier dans key_char() et une ligne dans show_help().
 *
 * FICHIER D'ETAT : %APPDATA%\MiniCalc\MiniCalc.dat (voir save_state / load_state), texte
 *   versionne. Les versions anciennes l'ecrivaient dans %APPDATA%\MiniCalc.dat : relu au besoin.
 *
 * ENCODAGE : les commentaires sont volontairement sans accents (lisibles partout). Dans le
 *   code, les textes accentues utilisent \xE9 (chaines ANSI, ex. "Copi\xE9") ou \u00E9
 *   (chaines Unicode L"...").
 *
 * Compilation avec Visual C++ (Visual Studio / VS Code) : double-cliquer sur build.bat (il
 * compile aussi l'icone). La tache VS Code par defaut "cl.exe build active file" marche
 * egalement, mais produit un exe sans icone.
 *
 * Compilation (MinGW, exe 32 bits qui tourne sur tout Windows) :
 *   i686-w64-mingw32-windres minicalc.rc -O coff -o minicalc_res.o
 *   i686-w64-mingw32-gcc -Os -s -static -mwindows minicalc.c minicalc_res.o -o MiniCalc.exe -lgdi32 -luser32 -lm
 * Test du moteur seul (sans fenetre), sous Linux :
 *   gcc -DTEST minicalc.c -o t -lm && ./t "2@3@" "+"
 */
#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS    /* Visual C++ : pas d'avertissements sur fopen, getenv, strcpy... */
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef TEST
#include <windows.h>
#ifdef _MSC_VER
/* Visual C++ : le fichier demande lui-meme au linker les bibliotheques Windows qu'il utilise.
 * Ainsi la compilation marche meme avec la tache VS Code par defaut "cl.exe build active file",
 * qui ne les ajoute pas (sans cela : erreurs LNK2019 "symbole externe non resolu"). Ignore par MinGW. */
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif
#endif

/* ------------------------------------------------------------------ */
/*  Moteur RPN                                                         */
/* ------------------------------------------------------------------ */
#define MAXS 512                 /* taille maximale de la pile */
static double st[MAXS];          /* la pile : st[n-1] = niveau 1 (sommet), st[0] = fond */
static int    n = 0;             /* nombre de valeurs actuellement sur la pile */
static double regA = 0, regB = 0;/* memoires A et B (commandes STO_A, RCL_A, STO_B, RCL_B) */
static int    angDeg = 1;        /* mode angle : 1 = degres (defaut), 0 = radians */
static int    topmost = 0;       /* 1 = fenetre toujours au premier plan */
static char   ent[48];           /* saisie en cours : caracteres tapes, pas encore sur la pile */
static int    elen = 0;          /* longueur de ent ; 0 = aucune saisie en cours */
static char   msg[64] = "";      /* message court affiche dans la zone grise (erreur, "Copié"...) */
static const double PI_ = 3.14159265358979323846;

/* Memorise un message court. La zone grise affiche environ 24 caracteres au maximum. */
static void setmsg(const char *s) { snprintf(msg, sizeof msg, "%s", s); }

/* Ajoute v au sommet de la pile. Renvoie 0 (avec un message) si la pile est pleine. */
static int push(double v)
{
    if (n >= MAXS) { setmsg("Pile pleine"); return 0; }
    st[n++] = v;
    return 1;
}

/* Verifie qu'il y a au moins k valeurs sur la pile. Sinon : message d'erreur et renvoie 0.
 * A appeler avant de lire st[n-1], st[n-2]... pour ne jamais sortir du tableau. */
static int need(int k)
{
    if (n < k) { setmsg("Manque argument"); return 0; }
    return 1;
}

/* Convertit la saisie en cours (ent) en nombre et la pousse sur la pile ; sans effet s'il n'y
 * a pas de saisie. Presque toutes les actions commencent par la : ainsi "5 puis une fonction"
 * marche sans devoir appuyer sur Entree. */
static void commit(void)
{
    if (elen) {
        double v = strtod(ent, NULL);
        elen = 0; ent[0] = 0;
        push(v);
    }
}

/* Ecrit v dans buf avec prec chiffres significatifs (format %g : pas de zeros inutiles).
 * Gere nan / inf et evite l'affichage "-0". Sert a l'affichage et aux tests. */
static void fmtp(double v, char *buf, size_t sz, int prec)
{
    if (isnan(v))      snprintf(buf, sz, "(nan)");
    else if (isinf(v)) snprintf(buf, sz, v < 0 ? "-(inf)" : "(inf)");
    else {
        if (v == 0) v = 0;                 /* evite "-0" */
        snprintf(buf, sz, "%.*g", prec, v);
    }
}

/* Formatage standard : 15 chiffres significatifs. */
static void fmt(double v, char *buf, size_t sz) { fmtp(v, buf, sz, 15); }

/* Dans do_cmd : vrai si la commande recue (c) est egale a la chaine s. */
#define IS(s) (strcmp(c, s) == 0)
/* UN(expr) : fonction a un argument. Lit x = niveau 1, calcule r = expr, remplace le niveau 1
 * par r, puis QUITTE do_cmd. Refuse les resultats non finis (nan / inf) sans toucher a la pile.
 * Attention : les tests de domaine (ln de 0, racine de negatif...) se font AVANT UN(). */
#define UN(expr) do { if (!need(1)) return; x = st[n-1]; r = (expr); \
        if (!isfinite(r)) { setmsg("Erreur math"); return; } \
        st[n-1] = r; return; } while (0)

/* Conversions selon le mode angle courant : to_rad transforme l'angle saisi par
 * l'utilisateur en radians (pour sin / cos / tan) ; from_rad fait l'inverse (asin / acos / atan). */
static double to_rad(double a) { return angDeg ? a * PI_ / 180.0 : a; }
static double from_rad(double a) { return angDeg ? a * 180.0 / PI_ : a; }

/* Execute une commande sur la pile. C'est LE point d'entree du moteur.
 * Conventions : x = niveau 1 (sommet), y = niveau 2. En cas d'erreur (pile trop courte,
 * division par 0...) on affiche un message avec setmsg() et on revient SANS modifier la pile.
 * Une commande inconnue est ignoree sans erreur. */
static void do_cmd(const char *c)
{
    double x, y, r, a, b, d;
    int i, k;

    commit();
    msg[0] = 0;

    /* ---- operations a deux arguments (y op x) ----
     * Le resultat remplace les deux operandes. Pour "-" et "/" : y - x et y / x. */
    if (IS("+") || IS("-") || IS("*") || IS("/") || IS("x^y") ||
        IS("MOD") || IS("Cnp")) {
        if (!need(2)) return;
        y = st[n-2]; x = st[n-1];
        if      (IS("+"))   r = y + x;
        else if (IS("-"))   r = y - x;
        else if (IS("*"))   r = y * x;
        else if (IS("/"))   { if (x == 0) { setmsg("Division par 0"); return; } r = y / x; }
        else if (IS("x^y")) r = pow(y, x);
        else if (IS("MOD")) { if (x == 0) { setmsg("Division par 0"); return; }
                              r = y - x * floor(y / x); }
        else { /* Cnp : y = n, x = p */
            if (y < 0 || x < 0 || x > y || y > 1000 || y != floor(y) || x != floor(x)) {
                setmsg("Cnp: n>=p>=0"); return;
            }
            /* C(n,p) = produit des (n-p+i)/i pour i = 1..p : evite de calculer de grosses
             * factorielles. L'arrondi final corrige les petites erreurs de virgule flottante. */
            r = 1;
            for (i = 1; i <= (int)x; i++) r = r * (y - x + i) / i;
            r = floor(r + 0.5);
        }
        if (!isfinite(r)) { setmsg("Erreur math"); return; }
        n -= 2; push(r);
        return;
    }

    /* ---- fonctions a un argument ----
     * Chaque ligne = un test de domaine eventuel, puis UN(formule en x). */
    if (IS("NEG"))  UN(-x);
    if (IS("1/x"))  { if (need(1) && st[n-1] == 0) { setmsg("Division par 0"); return; } UN(1.0 / x); }
    if (IS("Sqr"))  { if (need(1) && st[n-1] < 0)  { setmsg("Sqr: x < 0"); return; } UN(sqrt(x)); }
    if (IS("^2"))   UN(x * x);
    if (IS("e^x"))  UN(exp(x));
    if (IS("ln"))   { if (need(1) && st[n-1] <= 0) { setmsg("ln: x > 0"); return; } UN(log(x)); }
    if (IS("10^x")) UN(pow(10.0, x));
    if (IS("Log"))  { if (need(1) && st[n-1] <= 0) { setmsg("log: x > 0"); return; } UN(log10(x)); }
    if (IS("SIN"))  UN(sin(to_rad(x)));
    if (IS("COS"))  UN(cos(to_rad(x)));
    if (IS("TAN"))  UN(tan(to_rad(x)));
    if (IS("ASIN")) UN(from_rad(asin(x)));
    if (IS("ACOS")) UN(from_rad(acos(x)));
    if (IS("ATAN")) UN(from_rad(atan(x)));
    if (IS("D->R")) UN(x * PI_ / 180.0);
    if (IS("R->D")) UN(x * 180.0 / PI_);
    if (IS("ABS"))  UN(fabs(x));
    if (IS("FACT")) {
        if (!need(1)) return;
        x = st[n-1];
        /* 170! est la plus grande factorielle representable dans un double (171! deborde) */
        if (x < 0 || x > 170 || x != floor(x)) { setmsg("FACT: 0..170"); return; }
        r = 1;
        for (i = 2; i <= (int)x; i++) r *= i;
        st[n-1] = r;
        return;
    }

    /* ---- constantes et registres ----
     * STO copie le niveau 1 dans la memoire (il reste sur la pile) ; RCL pose la memoire au sommet. */
    if (IS("PI"))    { push(PI_); return; }
    if (IS("STO_A")) { if (need(1)) { regA = st[n-1]; setmsg("A memorise"); } return; }
    if (IS("STO_B")) { if (need(1)) { regB = st[n-1]; setmsg("B memorise"); } return; }
    if (IS("RCL_A")) { push(regA); return; }
    if (IS("RCL_B")) { push(regB); return; }

    /* ---- pourcentages ----
     * y = valeur de reference (niveau 2), x = pourcentage (niveau 1). */
    if (IS("x%y")) {                    /* x% de y ; y est conserve */
        if (!need(2)) return;
        st[n-1] = st[n-2] * st[n-1] / 100.0;
        return;
    }
    if (IS("d%")) {                     /* variation en % de y vers x */
        if (!need(2)) return;
        y = st[n-2]; x = st[n-1];
        if (y == 0) { setmsg("Division par 0"); return; }
        n -= 2; push(100.0 * (x - y) / y);
        return;
    }
    if (IS("x-y%") || IS("x+y%")) {     /* y diminue / augmente de x % */
        if (!need(2)) return;
        y = st[n-2]; x = st[n-1];
        r = IS("x-y%") ? y * (1.0 - x / 100.0) : y * (1.0 + x / 100.0);
        n -= 2; push(r);
        return;
    }

    /* ---- manipulation de la pile ----
     * Niveau 1 = sommet. DUP2 / OVER / ROT agissent sur les niveaux 1 a 3. */
    if (IS("DUP"))   { if (need(1)) push(st[n-1]); return; }
    if (IS("DUP2"))  { if (need(2)) { a = st[n-2]; b = st[n-1]; push(a); push(b); } return; }
    if (IS("DROP"))  { if (need(1)) n--; return; }
    if (IS("DROP2")) { if (need(2)) n -= 2; return; }
    if (IS("CLEAR")) { n = 0; return; }
    if (IS("SWAP"))  { if (need(2)) { a = st[n-1]; st[n-1] = st[n-2]; st[n-2] = a; } return; }
    if (IS("OVER"))  { if (need(2)) push(st[n-2]); return; }
    if (IS("ROT"))   {                  /* niveau 3 -> niveau 1 */
        if (need(3)) { a = st[n-3]; st[n-3] = st[n-2]; st[n-2] = st[n-1]; st[n-1] = a; }
        return;
    }
    if (IS("AROT"))  {                  /* niveau 1 -> niveau 3 */
        if (need(3)) { a = st[n-1]; st[n-1] = st[n-2]; st[n-2] = st[n-3]; st[n-3] = a; }
        return;
    }
    if (IS("PICK")) {                   /* le niveau 1 donne n ; on le retire, puis on copie le niveau n */
        if (!need(1)) return;
        k = (int)st[n-1];
        if (k < 1 || k > n - 1) { setmsg("PICK invalide"); return; }
        n--;
        push(st[n-k]);
        return;
    }
    if (IS("DEPTH")) { d = n; push(d); return; }

    /* ---- statistiques ----
     * MEAN et SUM portent sur TOUTE la pile, qui est videe puis remplacee par le resultat. */
    if (IS("MEAN") || IS("SUM")) {
        if (!need(1)) return;
        r = 0;
        for (i = 0; i < n; i++) r += st[i];
        if (IS("MEAN")) r /= n;
        n = 0; push(r);
        return;
    }

    /* ---- a x^2 + b x + c = 0  (a = niveau 3, b = niveau 2, c = niveau 1) ----
     * Racines = (-b +/- racine(b^2 - 4ac)) / 2a. Si a = 0, equation du premier degre.
     * Discriminant negatif : on empile la partie reelle puis la partie imaginaire (>= 0). */
    if (IS("QUAD")) {
        if (!need(3)) return;
        a = st[n-3]; b = st[n-2]; d = st[n-1];
        if (a == 0) {
            if (b == 0) { setmsg("Pas de solution"); return; }
            n -= 3; push(-d / b);
            return;
        }
        r = b * b - 4 * a * d;
        if (r >= 0) {
            r = sqrt(r);
            n -= 3; push((-b + r) / (2 * a)); push((-b - r) / (2 * a));
        } else {
            n -= 3; push(-b / (2 * a)); push(sqrt(-r) / (2 * fabs(a)));
            setmsg("complexe re,im");
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/*  Presse-papiers (Windows uniquement)                                */
/* ------------------------------------------------------------------ */
#ifndef TEST
static HWND hMain;                 /* fenetre principale (necessaire pour ouvrir le presse-papiers) */

/* Place v dans le presse-papiers sous forme de texte (15 chiffres significatifs, point
 * decimal). Renvoie 1 si la copie a reussi. Utilisee par "Copier" et par le double-clic. */
static int clip_set(double v)
{
    char buf[64];
    HGLOBAL h;
    char *p;
    snprintf(buf, sizeof buf, "%.15g", v);
    if (!OpenClipboard(hMain)) return 0;
    EmptyClipboard();
    h = GlobalAlloc(GMEM_MOVEABLE, strlen(buf) + 1);
    if (h) {
        p = (char *)GlobalLock(h);
        strcpy(p, buf);
        GlobalUnlock(h);
        SetClipboardData(CF_TEXT, h);
    }
    CloseClipboard();
    return 1;
}

/* Copie le niveau 1 (la saisie en cours est d'abord validee). */
static void clip_copy(void)
{
    commit();
    if (!need(1)) return;
    if (clip_set(st[n-1])) setmsg("Copi\xE9");
}

/* Colle le texte du presse-papiers comme nombre sur la pile. La virgule est acceptee comme
 * separateur decimal ; un texte qui ne commence pas par un nombre est refuse. */
static void clip_paste(void)
{
    HANDLE h;
    char *p, tmp[96], *e;
    double v;
    size_t i;
    if (!OpenClipboard(hMain)) return;
    h = GetClipboardData(CF_TEXT);
    if (h && (p = (char *)GlobalLock(h)) != NULL) {
        snprintf(tmp, sizeof tmp, "%s", p);
        GlobalUnlock(h);
        for (i = 0; tmp[i]; i++) if (tmp[i] == ',') tmp[i] = '.';
        v = strtod(tmp, &e);
        if (e != tmp) { commit(); push(v); }
        else setmsg("Pas un nombre");
    }
    CloseClipboard();
}
#else
/* Mode TEST : pas de presse-papiers, on fournit des fonctions vides pour que le moteur compile. */
static int  clip_set(double v) { (void)v; return 1; }
static void clip_copy(void)  { commit(); }
static void clip_paste(void) { }
#endif

/* ------------------------------------------------------------------ */
/*  Clavier : un caractere (deja traduit par le clavier de l'utilisateur) */
/* ------------------------------------------------------------------ */

/* Traite un caractere tape. ch vient de WM_CHAR : c'est le caractere REELLEMENT produit par
 * la touche, quelle que soit la disposition du clavier (AZERTY, QWERTY, avec ou sans pave
 * numerique). C'est ce qui corrige le bug d'origine (le 8 refuse sur les portables).
 * Saisie : chiffres et un seul separateur decimal (point ou virgule), 30 caracteres maximum.
 * Toutes les autres touches sont des raccourcis vers do_cmd(). */
static void key_char(int ch)
{
    msg[0] = 0;
    if (ch >= '0' && ch <= '9') {
        if (elen < 30) { ent[elen++] = (char)ch; ent[elen] = 0; }
        return;
    }
    switch (ch) {
    case '.': case ',':
        if (!strchr(ent, '.') && elen < 30) {
            if (elen == 0) { strcpy(ent, "0."); elen = 2; }
            else { ent[elen++] = '.'; ent[elen] = 0; }
        }
        return;
    case '\r': case '\n':                      /* Entree : valide la saisie, ou duplique le niveau 1 */
        if (elen) commit(); else do_cmd("DUP");
        return;
    case '\b':                                  /* Retour arriere : efface un caractere, ou DROP */
        if (elen) ent[--elen] = 0; else do_cmd("DROP");
        return;
    case 27:                                   /* Echap : annule la saisie */
        elen = 0; ent[0] = 0;
        return;
    /* operateurs : "x" sert de multiplication pour les claviers sans pave numerique */
    case '+': do_cmd("+"); return;
    case '-': do_cmd("-"); return;
    case '*': case 'x': case 'X': do_cmd("*"); return;
    case '/': do_cmd("/"); return;
    case '^': do_cmd("x^y"); return;
    case 'n': case 'N':                        /* change le signe (saisie ou niveau 1) */
        if (elen) {                            /* change le signe de la saisie */
            if (ent[0] == '-') { memmove(ent, ent + 1, (size_t)elen); elen--; }
            else { memmove(ent + 1, ent, (size_t)elen + 1); ent[0] = '-'; elen++; }
        } else do_cmd("NEG");
        return;
    /* raccourcis lettres ; 3 et 22 = Ctrl+C et Ctrl+V */
    case 'i': case 'I': do_cmd("1/x");  return;
    case 'd': case 'D': do_cmd("DUP2"); return;
    case 's': case 'S': do_cmd("SWAP"); return;
    case 'm': case 'M': angDeg = !angDeg; return;
    case 'c': case 'C': case 3:  clip_copy();  return;
    case 'p': case 'P': case 22: clip_paste(); return;
    }
}


/* ------------------------------------------------------------------ */
/*  Banc de test du moteur (Linux)                                     */
/* ------------------------------------------------------------------ */
#ifdef TEST
/* Banc de test : chaque argument est soit une commande precedee de "!" (ex. "!SWAP"),
 * soit une suite de touches tapees une a une ("@" = Entree). Affiche la pile a la fin.
 * Exemple : ./t "2@3@" "!SWAP" "-"   */
int main(int argc, char **argv)
{
    int i, j;
    char b[64];
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '!') do_cmd(argv[i] + 1);
        else for (j = 0; argv[i][j]; j++) key_char(argv[i][j] == '@' ? '\r' : argv[i][j]);
    }
    commit();
    for (i = 0; i < n; i++) { fmt(st[i], b, sizeof b); printf("%d: %s\n", n - i, b); }
    if (msg[0]) printf("msg: %s\n", msg);
    return 0;
}
#else
/* ------------------------------------------------------------------ */
/*  Interface Windows                                                  */
/* ------------------------------------------------------------------ */
/* Identifiants des boutons (recus dans WM_COMMAND / WM_DRAWITEM) */
#define ID_STK   100   /* bouton STK (liste deroulante de la pile) */
#define ID_ADD   111   /* boutons carres + - * / (masques par defaut) */
#define ID_SUB   112
#define ID_MUL   113
#define ID_DIV   114

/* ---- Disposition de la fenetre (tout est en pixels, repere = zone cliente) ----
 *
 *   +----------------------------------+------+--+
 *   | MENU v                    DEG    | /    |  |  <- ligne d'etat (STATUS_H)
 *   | 6:                               | *    |l |
 *   | ...   6 niveaux de pile          | -    |a |  <- rcDisp (l'ecran blanc) ; les 4 boutons
 *   | 1:                               | +    |n |     carres n'existent que si opsShown
 *   +----------------------------------+------+g |
 *   | zone de saisie / messages (grise)| STK  |u |  <- rcEntry et bouton STK
 *   +----------------------------------+------+--+     et la languette rcStrip (clic = deplier)
 */
#define STATUS_H 14      /* hauteur de la ligne d'etat (menu a gauche, DEG / RAD a droite) */
#define LINE_H   16      /* hauteur d'une ligne de pile */
#define NLEVELS  6       /* niveaux de pile affiches */
#define CW 262           /* largeur de la zone cliente */
#define CH 152           /* hauteur de la zone cliente */
#define ROWR 241         /* bord droit du contenu (la languette est a droite de cette limite) */
static RECT rcDisp  = {6, 6, ROWR, 116};    /* ecran de la pile (se retrecit si + - * / sont affiches) */
static RECT rcEntry = {6, 122, 194, 146};   /* zone de saisie (grise) */
static RECT rcStrip = {246, 6, 256, 146};   /* languette pour deplier / replier + - * / */
static int  opsShown = 0;                   /* 0 = touches + - * / masquees (defaut) */
/* Couleurs reprises de l'ancien programme VB */
#define COL_GREEN  RGB(199, 254, 188)       /* vert clair : touches + - * / */
#define COL_TEAL   RGB(81, 182, 184)        /* bleu-vert : bouton STK */
#define COL_GRAY   RGB(172, 172, 172)       /* fond de la zone de saisie */
#define COL_BORDER RGB(91, 91, 91)          /* contour des touches et de la zone de saisie */
static int flashId = 0;                     /* id de la touche + - * / en surbrillance (0 = aucune) */
#define SQ 24            /* cote des boutons d'operation */
#define SQ_X (ROWR - SQ) /* abscisse des boutons carres (colles au bord droit du contenu) */
static HWND hOps[4];     /* les 4 boutons carres : / * - + (de haut en bas) */
static HFONT fMono;      /* police de l'ecran et de la zone de saisie (Courier New) */
static HFONT fMenu;      /* police des listes deroulantes */
static HFONT fSup;       /* petite police des exposants et indices (listes deroulantes) */

/* Chemin du fichier d'etat : %APPDATA%\MiniCalc\MiniCalc.dat (a defaut, dans le dossier
 * courant). Le dossier MiniCalc est cree par save_state s'il n'existe pas. */
static void data_path(char *out, size_t sz)
{
    const char *ad = getenv("APPDATA");
    if (ad && *ad) snprintf(out, sz, "%s\\MiniCalc\\MiniCalc.dat", ad);
    else snprintf(out, sz, "MiniCalc.dat");
}

/* Ancien emplacement des versions precedentes : %APPDATA%\MiniCalc.dat (sans sous-dossier).
 * Relu une fois, si le nouveau fichier n'existe pas encore, pour ne pas perdre la pile ni
 * les reglages ; la sauvegarde suivante ecrit dans le nouvel emplacement. Renvoie 0 si
 * APPDATA n'existe pas. */
static int old_data_path(char *out, size_t sz)
{
    const char *ad = getenv("APPDATA");
    if (!(ad && *ad)) return 0;
    snprintf(out, sz, "%s\\MiniCalc.dat", ad);
    return 1;
}

/* Ecrit l'etat a la fermeture. Format texte :
 *   ligne 1 : "v3 topmost angDeg regA regB n opsShown"
 *   puis n lignes : les valeurs de la pile, du fond vers le sommet.
 * Si on ajoute un champ, incrementer la version (v4...) et adapter load_state.
 * Le fichier est %APPDATA%\MiniCalc\MiniCalc.dat (dossier cree au besoin). */
static void save_state(void)
{
    char path[MAX_PATH + 32];
    FILE *f;
    int i;
    data_path(path, sizeof path);
    {   /* cree le dossier %APPDATA%\MiniCalc s'il manque (sans effet s'il existe deja) */
        char dir[MAX_PATH + 32], *p;
        snprintf(dir, sizeof dir, "%s", path);
        p = strrchr(dir, '\\');
        if (p) { *p = 0; CreateDirectoryA(dir, NULL); }
    }
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "v3 %d %d %.17g %.17g %d %d\n", topmost, angDeg, regA, regB, n, opsShown);
    for (i = 0; i < n; i++) fprintf(f, "%.17g\n", st[i]);
    fclose(f);
}

/* Relit l'etat au demarrage ; ignore silencieusement un fichier absent ou illisible.
 * Compatible avec les anciennes versions : v1 n'avait pas d'angle (on force les degres),
 * v2 n'avait pas opsShown (les touches + - * / restent masquees). */
static void load_state(void)
{
    char path[MAX_PATH + 32];
    FILE *f;
    int i, cnt = 0, tm = 0, ad = 0, ver = 0;
    double ra = 0, rb = 0, v;
    data_path(path, sizeof path);
    f = fopen(path, "r");
    if (!f && old_data_path(path, sizeof path)) f = fopen(path, "r");   /* ancien emplacement */
    if (!f) return;
    if (fscanf(f, "v%d %d %d %lf %lf %d", &ver, &tm, &ad, &ra, &rb, &cnt) == 6) {
        topmost = tm; angDeg = (ver >= 2) ? ad : 1; regA = ra; regB = rb;
        if (ver >= 3) { int ex = 0; if (fscanf(f, "%d", &ex) == 1) opsShown = ex; }
        for (i = 0; i < cnt && i < MAXS; i++) {
            if (fscanf(f, "%lf", &v) != 1) break;
            st[n++] = v;
        }
    }
    fclose(f);
}

/* Redessine l'ecran et la zone de saisie (sans effacer le fond : le dessin est double-bufferise). */
static void refresh(HWND hw)
{
    InvalidateRect(hw, &rcDisp, FALSE);
    InvalidateRect(hw, &rcEntry, FALSE);
}

/* Applique l'etat opsShown : montre ou cache les 4 boutons carres et ajuste la largeur de
 * l'ecran (il est plus etroit quand les boutons sont affiches), puis redessine tout. */
static void apply_layout(HWND hw)
{
    int i;
    rcDisp.right = opsShown ? SQ_X - 4 : ROWR;
    for (i = 0; i < 4; i++)
        if (hOps[i]) ShowWindow(hOps[i], opsShown ? SW_SHOWNA : SW_HIDE);
    InvalidateRect(hw, NULL, TRUE);
}

/* Plie / deplie les boutons + - * / (languette, double-clic ou menu clic droit). */
static void toggle_ops(HWND hw)
{
    opsShown = !opsShown;
    apply_layout(hw);
}

/* Met la fenetre au premier plan (ou non) selon topmost, sans la deplacer ni la redimensionner. */
static void set_topmost(HWND hw)
{
    SetWindowPos(hw, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* Boite d'aide : liste des raccourcis. A tenir a jour quand on ajoute une touche dans key_char(). */
static void show_help(HWND hw)
{
    MessageBoxA(hw,
        "Clavier (fonctionne avec ou sans pave numerique) :\n\n"
        "0-9 . ,      saisie du nombre\n"
        "Entree       valide la saisie (ou DUP si rien n'est saisi)\n"
        "+ - * /      operations (x ou X = multiplication)\n"
        "^            x^y\n"
        "n ou fleche gauche  change le signe (saisie en cours, ou niveau 1)\n"
        "i            1/x\n"
        "d            DUP2\n"
        "s ou fleche droite   SWAP\n"
        "Retour arr.  efface un caractere (ou DROP)\n"
        "Suppr        DROP\n"
        "Echap        annule la saisie\n"
        "c / p        copier le sommet / coller un nombre\n"
        "m            bascule radians / degres (clic sur RAD/DEG aussi)\n"
        "Double-clic sur une ligne de la pile : copie sa valeur\n"
        "Clic sur MENU (en haut a gauche) : fonctions mathematiques en 4 colonnes\n"
        "Bouton STK : fonctions de pile (SWAP, DUP2, OVER, ROT, AROT, PICK, DEPTH)\n"
        "Languette a droite : affiche / masque les touches + - * /\n"
        "Clic droit   menu : copier, coller, toujours au premier plan...\n\n"
        "ax2+bx+c : a, b, c sur les niveaux 3, 2, 1 -> racines.\n"
        "Cnp : n au niveau 2, p au niveau 1 ; ! = factorielle.\n"
        "x%y garde y ; d% = variation de y vers x en %.",
        "MiniCalc - aide", MB_OK);
}

/* Boite "A propos" (version Unicode pour les accents). */
static void show_about(HWND hw)
{
    MessageBoxW(hw, L"Calculatrice Polonaise Inverse vibecod\u00E9e avec Claude Sonnet 5.5",
                L"\u00C0 propos de MiniCalc RPN", MB_OK | MB_ICONINFORMATION);
}

/* Menu du clic droit : options generales. TrackPopupMenu renvoie directement l'identifiant
 * choisi (TPM_RETURNCMD), traite dans le switch ci-dessous. */
static void context_menu(HWND hw)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    int cmd;
    AppendMenuA(m, MF_STRING, 1, "Copier le sommet");
    AppendMenuA(m, MF_STRING, 2, "Coller");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING | (topmost ? MF_CHECKED : 0), 3, "Toujours au premier plan");
    AppendMenuA(m, MF_STRING | (opsShown ? MF_CHECKED : 0), 8, "Afficher les touches + - * /");
    AppendMenuA(m, MF_STRING, 4, "Effacer la pile");
    AppendMenuA(m, MF_STRING, 5, angDeg ? "Passer en radians" : "Passer en degres");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, 6, "Aide");
    AppendMenuW(m, MF_STRING, 7, L"\u00C0 propos");
    GetCursorPos(&pt);
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hw, NULL);
    DestroyMenu(m);
    switch (cmd) {
    case 1: clip_copy(); break;
    case 2: clip_paste(); break;
    case 3: topmost = !topmost; set_topmost(hw); break;
    case 4: commit(); n = 0; break;
    case 5: angDeg = !angDeg; break;
    case 6: show_help(hw); break;
    case 7: show_about(hw); break;
    case 8: toggle_ops(hw); break;
    }
}

/* ---- liste deroulante MENU : 5 colonnes de fonctions ----
 * Les elements sont "dessines a la main" (MF_OWNERDRAW) pour pouvoir afficher exposants et
 * indices (x puissance y, C(n,p)...) sans dependre des glyphes de la police. Windows demande
 * d'abord la taille de chaque element (WM_MEASUREITEM) puis son dessin (WM_DRAWITEM). */
typedef struct { const wchar_t *base, *sup, *sub, *tail; const char *cmd; } MItem;
/* Un element = base, puis exposant (sup) et indice (sub) alignes l'un sous l'autre, puis
 * tail (suite normale du libelle). cmd = commande envoyee a do_cmd() ; NULL = intitule grise
 * et non cliquable (ex. "arc"). Les champs inutiles valent NULL. */
/* Le tableau est lu de haut en bas, colonne apres colonne (voir colstart[]). */
static const MItem mitems[] = {
    /* colonne 1 */
    {L"\u00B1", NULL, NULL, NULL, "NEG"}, {L"1/x", NULL, NULL, NULL, "1/x"},
    {L"\u221A", NULL, NULL, NULL, "Sqr"}, {L"x", L"2", NULL, NULL, "^2"},
    {L"\u03C0", NULL, NULL, NULL, "PI"},
    /* colonne 2 */
    {L"x", L"y", NULL, NULL, "x^y"}, {L"e", L"x", NULL, NULL, "e^x"},
    {L"ln", NULL, NULL, NULL, "ln"}, {L"10", L"x", NULL, NULL, "10^x"},
    {L"Log", NULL, NULL, NULL, "Log"},
    /* colonne 3 : trigonometrie */
    {L"sin", NULL, NULL, NULL, "SIN"}, {L"cos", NULL, NULL, NULL, "COS"},
    {L"tan", NULL, NULL, NULL, "TAN"}, {L"arc", NULL, NULL, NULL, NULL},
    {L"asin", NULL, NULL, NULL, "ASIN"}, {L"acos", NULL, NULL, NULL, "ACOS"},
    {L"atan", NULL, NULL, NULL, "ATAN"},
    {L"DEG->RAD", NULL, NULL, NULL, "D->R"}, {L"RAD->DEG", NULL, NULL, NULL, "R->D"},
    /* colonne 4 : pourcentages */
    {L"x%y", NULL, NULL, NULL, "x%y"}, {L"d%", NULL, NULL, NULL, "d%"},
    {L"x-y%", NULL, NULL, NULL, "x-y%"}, {L"x+y%", NULL, NULL, NULL, "x+y%"},
    /* colonne 5 : combinaisons (C, p en exposant, n en indice), factorielle, second degre */
    {L"C", L"p", L"n", NULL, "Cnp"}, {L"!", NULL, NULL, NULL, "FACT"},
    {L"ax", L"2", NULL, L"+bx+c", "QUAD"},
};
#define NMITEMS ((int)(sizeof mitems / sizeof mitems[0]))   /* nombre d'elements du tableau */
/* Indice (dans mitems[]) du premier element de chaque colonne : une nouvelle colonne commence
 * par un saut de colonne (MF_MENUBARBREAK) a cet endroit. */
static const int colstart[5] = {0, 5, 10, 19, 23};
/* Les identifiants des elements vont de MENU_ID0 a MENU_ID0 + NMITEMS - 1 (ils ne doivent pas
 * valoir 0, qui signifie "menu ferme sans choix"). */
#define MENU_ID0 1000

/* Largeur en pixels du texte t dessine avec la police f (0 si t est NULL). */
static int text_w(HDC dc, HFONT f, const wchar_t *t)
{
    SIZE sz;
    HGDIOBJ o;
    if (!t) return 0;
    o = SelectObject(dc, f);
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    SelectObject(dc, o);
    return sz.cx;
}

/* Dessine un element de la liste MENU : fond (surligne si survole), base, puis exposant en
 * haut et indice en bas (alignes a gauche, a la suite de la base), puis tail. */
static void draw_menu_item(const DRAWITEMSTRUCT *di)
{
    const MItem *it = (const MItem *)di->itemData;
    HDC dc = di->hDC;
    RECT rc = di->rcItem;
    int sel = it && it->cmd && (di->itemState & ODS_SELECTED);
    HBRUSH br = CreateSolidBrush(GetSysColor(sel ? COLOR_HIGHLIGHT : COLOR_MENU));
    SIZE sz;
    int x, y, w1, w2;
    HGDIOBJ of;

    FillRect(dc, &rc, br);
    DeleteObject(br);
    if (!it) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : (it->cmd ? COLOR_MENUTEXT : COLOR_GRAYTEXT)));
    of = SelectObject(dc, fMenu);
    GetTextExtentPoint32W(dc, it->base, (int)wcslen(it->base), &sz);
    x = rc.left + 12;
    y = rc.top + (rc.bottom - rc.top - sz.cy) / 2;
    TextOutW(dc, x, y, it->base, (int)wcslen(it->base));
    x += sz.cx + 1;
    if (it->sup || it->sub) {       /* exposant en haut, indice en bas, alignes a gauche */
        SelectObject(dc, fSup);
        if (it->sup) TextOutW(dc, x, y - 2, it->sup, (int)wcslen(it->sup));
        if (it->sub) TextOutW(dc, x, y + 8, it->sub, (int)wcslen(it->sub));
        w1 = text_w(dc, fSup, it->sup);
        w2 = text_w(dc, fSup, it->sub);
        x += (w1 > w2 ? w1 : w2) + 1;
    }
    if (it->tail) {
        SelectObject(dc, fMenu);
        TextOutW(dc, x, y, it->tail, (int)wcslen(it->tail));
    }
    SelectObject(dc, of);
}

/* Ouvre la liste MENU juste sous le mot MENU et execute la commande choisie. Le saut de
 * colonne est ajoute au premier element de chaque colonne ; les intitules (cmd == NULL)
 * sont grises. */
static void menu_dropdown(HWND hw)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    int i, c, cmd;
    for (i = 0; i < NMITEMS; i++) {
        UINT fl = MF_OWNERDRAW;
        for (c = 1; c < 5; c++) if (i == colstart[c]) fl |= MF_MENUBARBREAK;
        if (!mitems[i].cmd) fl |= MF_GRAYED;
        AppendMenuW(m, fl, MENU_ID0 + i, (LPCWSTR)&mitems[i]);
    }
    pt.x = rcDisp.left; pt.y = rcDisp.top + STATUS_H;       /* juste sous le mot MENU */
    ClientToScreen(hw, &pt);
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hw, NULL);
    DestroyMenu(m);
    if (cmd >= MENU_ID0 && cmd < MENU_ID0 + NMITEMS && mitems[cmd - MENU_ID0].cmd)
        do_cmd(mitems[cmd - MENU_ID0].cmd);
}

/* ---- bouton STK : fonctions de pile ----
 * Liste simple (texte) ouverte sous le bouton, alignee a droite. Les commandes sont celles
 * de do_cmd() ; modifier names[] ET la borne 7 pour ajouter une fonction. */
static void stk_menu(HWND hw)
{
    static const char *const names[] = {"SWAP", "DUP2", "OVER", "ROT", "AROT", "PICK", "DEPTH"};
    HMENU m = CreatePopupMenu();
    POINT pt;
    int i, cmd;
    for (i = 0; i < 7; i++) AppendMenuA(m, MF_STRING, 1 + i, names[i]);
    pt.x = ROWR; pt.y = rcEntry.bottom;                     /* sous le bouton, aligne a droite */
    ClientToScreen(hw, &pt);
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hw, NULL);
    DestroyMenu(m);
    if (cmd >= 1 && cmd <= 7) do_cmd(names[cmd - 1]);
}

/* Dessine l'ecran blanc : ligne d'etat (MENU a gauche, DEG / RAD a droite) puis les NLEVELS
 * niveaux de pile. Technique du double buffer : on dessine d'abord dans un bitmap en memoire
 * (m), puis on le copie d'un seul coup a l'ecran (BitBlt) : aucun scintillement. */
static void draw_display(HDC dc)
{
    int w = rcDisp.right - rcDisp.left, h = rcDisp.bottom - rcDisp.top;
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ ob = SelectObject(m, bm), of;
    RECT r = {0, 0, w, h}, rr;
    HBRUSH br = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    char buf[64];
    int lvl;

    FillRect(m, &r, br);
    DeleteObject(br);
    SetBkMode(m, TRANSPARENT);
    of = SelectObject(m, fMono);

    /* ligne d'etat : menu a gauche, angle a droite */
    SetTextColor(m, GetSysColor(COLOR_WINDOWTEXT));
    rr.left = 4; rr.right = w - 4; rr.top = 0; rr.bottom = STATUS_H;
    DrawTextA(m, "MENU", -1, &rr, DT_LEFT | DT_SINGLELINE);
    DrawTextA(m, angDeg ? "DEG" : "RAD", -1, &rr, DT_RIGHT | DT_SINGLELINE);
    {   /* petite fleche : le nom du menu ouvre une liste deroulante */
        SIZE ts;
        POINT tp[3];
        HGDIOBJ ob2, op2;
        GetTextExtentPoint32A(m, "MENU", 4, &ts);
        tp[0].x = 4 + ts.cx + 5;  tp[0].y = 4;
        tp[1].x = 4 + ts.cx + 11; tp[1].y = 4;
        tp[2].x = 4 + ts.cx + 8;  tp[2].y = 8;
        ob2 = SelectObject(m, GetStockObject(BLACK_BRUSH));
        op2 = SelectObject(m, GetStockObject(BLACK_PEN));
        Polygon(m, tp, 3);
        SelectObject(m, ob2);
        SelectObject(m, op2);
    }

    /* niveaux de pile */
    for (lvl = NLEVELS; lvl >= 1; lvl--) {
        rr.left = 4; rr.right = w - 4;
        rr.top = STATUS_H + (NLEVELS - lvl) * LINE_H; rr.bottom = rr.top + LINE_H;
        snprintf(buf, sizeof buf, "%d:", lvl);
        DrawTextA(m, buf, -1, &rr, DT_LEFT | DT_SINGLELINE);
        if (lvl <= n) {
            /* Reduit le nombre de chiffres (15 puis 14, 13...) jusqu'a ce que le nombre tienne
             * dans la largeur : seul l'affichage est raccourci, pas la valeur. */
            int prec;
            SIZE sz;
            for (prec = 15; prec >= 4; prec--) {
                fmtp(st[n - lvl], buf, sizeof buf, prec);
                GetTextExtentPoint32A(m, buf, (int)strlen(buf), &sz);
                if (sz.cx <= w - 8 - 22) break;
            }
            DrawTextA(m, buf, -1, &rr, DT_RIGHT | DT_SINGLELINE);
        }
    }

    SelectObject(m, of);
    BitBlt(dc, rcDisp.left, rcDisp.top, w, h, m, 0, 0, SRCCOPY);
    {   /* cadre en creux autour de l'ecran */
        RECT fr = {rcDisp.left - 2, rcDisp.top - 2, rcDisp.right + 2, rcDisp.bottom + 2};
        DrawEdge(dc, &fr, EDGE_SUNKEN, BF_RECT);
    }
    SelectObject(m, ob);
    DeleteObject(bm);
    DeleteDC(m);
}

/* Zone grise : affiche la saisie en cours (alignee a droite) ou, a defaut, le dernier message
 * (en rouge, aligne a gauche). Meme technique de double buffer que draw_display. */
static void draw_entry(HDC dc)
{
    int w = rcEntry.right - rcEntry.left, h = rcEntry.bottom - rcEntry.top;
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ ob = SelectObject(m, bm), of;
    RECT r = {0, 0, w, h}, rr;
    HBRUSH br = CreateSolidBrush(COL_GRAY), bb = CreateSolidBrush(COL_BORDER);

    FillRect(m, &r, br);
    FrameRect(m, &r, bb);
    DeleteObject(br);
    DeleteObject(bb);
    SetBkMode(m, TRANSPARENT);
    of = SelectObject(m, fMono);
    rr.left = 4; rr.right = w - 4; rr.top = 0; rr.bottom = h;
    if (elen) {
        SetTextColor(m, RGB(0, 0, 0));
        DrawTextA(m, ent, -1, &rr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    } else if (msg[0]) {
        SetTextColor(m, RGB(120, 0, 0));
        DrawTextA(m, msg, -1, &rr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    SelectObject(m, of);
    BitBlt(dc, rcEntry.left, rcEntry.top, w, h, m, 0, 0, SRCCOPY);
    SelectObject(m, ob);
    DeleteObject(bm);
    DeleteDC(m);
}

/* Languette fine a droite : fleche vers la gauche = "deplier" les boutons, vers la droite =
 * "replier". Un clic n'importe ou dans cette bande (et sa marge) bascule l'etat. */
static void draw_strip(HDC dc)
{
    HBRUSH br = CreateSolidBrush(RGB(205, 205, 205)), bb = CreateSolidBrush(COL_BORDER);
    POINT p[3];
    int cx = (rcStrip.left + rcStrip.right) / 2, cy = (rcStrip.top + rcStrip.bottom) / 2;
    HGDIOBJ ob, op;
    FillRect(dc, &rcStrip, br);
    FrameRect(dc, &rcStrip, bb);
    DeleteObject(br);
    DeleteObject(bb);
    if (opsShown) {         /* > : replier */
        p[0].x = cx - 2; p[0].y = cy - 5;
        p[1].x = cx - 2; p[1].y = cy + 5;
        p[2].x = cx + 2; p[2].y = cy;
    } else {                /* < : deplier */
        p[0].x = cx + 2; p[0].y = cy - 5;
        p[1].x = cx + 2; p[1].y = cy + 5;
        p[2].x = cx - 2; p[2].y = cy;
    }
    ob = SelectObject(dc, GetStockObject(BLACK_BRUSH));
    op = SelectObject(dc, GetStockObject(BLACK_PEN));
    Polygon(dc, p, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

/* Repeint les trois zones dessinees a la main (les boutons se repeignent eux-memes). */
static void paint(HWND hw)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hw, &ps);
    draw_display(dc);
    draw_entry(dc);
    draw_strip(dc);
    EndPaint(hw, &ps);
}

/* Cree un bouton "dessine a la main" (BS_OWNERDRAW) : son apparence plate verte ou bleu-vert
 * est produite dans WM_DRAWITEM. Les clics arrivent dans WM_COMMAND avec l'identifiant id. */
static HWND mkbtn(HWND hw, const char *txt, int id, int x, int y, int w, int h)
{
    HWND b = CreateWindowA("BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                           x, y, w, h, hw, (HMENU)(INT_PTR)id,
                           GetModuleHandle(NULL), NULL);
    SendMessage(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return b;
}

/* Surbrillance breve (130 ms) du bouton id quand la touche correspondante est tapee au
 * clavier. Le WM_TIMER numero 1 (voir WndProc) l'eteint ; refrapper une touche relance le delai. */
static void flash_btn(HWND hw, int id)
{
    HWND b;
    if (flashId && (b = GetDlgItem(hw, flashId)) != NULL) InvalidateRect(b, NULL, FALSE);
    flashId = id;
    b = GetDlgItem(hw, id);
    if (b) { InvalidateRect(b, NULL, FALSE); UpdateWindow(b); }
    SetTimer(hw, 1, 130, NULL);
}

/* Procedure de fenetre : recoit tous les evenements. Chaque "case" traite un type de message.
 * Apres une action qui modifie la pile, on appelle refresh() (redessin) et, apres un clic,
 * SetFocus(hw) pour que le clavier continue d'agir sur la calculatrice et non sur un bouton. */
static LRESULT CALLBACK WndProc(HWND hw, UINT msgid, WPARAM wp, LPARAM lp)
{
    int id;
    switch (msgid) {
    case WM_CREATE:                 /* creation : polices, bouton STK, boutons carres */
        hMain = hw;
        fMono = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
        fMenu = CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        fSup  = CreateFontA(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        mkbtn(hw, "STK", ID_STK, ROWR - 43, 122, 43, 24);          /* bouton bleu-vert */
        /* a droite de l'ecran (masques par defaut), de haut en bas : / * - + */
        hOps[0] = mkbtn(hw, "\xF7", ID_DIV, SQ_X, 6,       SQ, SQ);
        hOps[1] = mkbtn(hw, "\xD7", ID_MUL, SQ_X, 6 + 29,  SQ, SQ);
        hOps[2] = mkbtn(hw, "-",    ID_SUB, SQ_X, 6 + 58,  SQ, SQ);
        hOps[3] = mkbtn(hw, "+",    ID_ADD, SQ_X, 116 - SQ, SQ, SQ);
        apply_layout(hw);
        return 0;

    case WM_MEASUREITEM: {          /* taille des elements de la liste MENU */
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_MENU && mi->itemData) {
            const MItem *it = (const MItem *)mi->itemData;
            HDC dc = GetDC(hw);
            int ws1 = text_w(dc, fSup, it->sup), ws2 = text_w(dc, fSup, it->sub);
            int wd = text_w(dc, fMenu, it->base) + (ws1 > ws2 ? ws1 : ws2)
                   + text_w(dc, fMenu, it->tail) + 30;
            mi->itemWidth = wd < 62 ? 62 : wd;
            mi->itemHeight = 24;
            ReleaseDC(hw, dc);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {             /* touches plates, couleurs de l'ancien programme */
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lp;
        COLORREF col = (di->CtlID == ID_STK) ? COL_TEAL : COL_GREEN;
        if (di->CtlType == ODT_MENU) { draw_menu_item(di); return TRUE; }
        HBRUSH fb, bb = CreateSolidBrush(COL_BORDER);
        RECT rc = di->rcItem;
        HGDIOBJ of;
        char t[32];
        int pressed = (di->itemState & ODS_SELECTED) || (int)di->CtlID == flashId;
        if (pressed)
            col = RGB(GetRValue(col) * 3 / 4, GetGValue(col) * 3 / 4, GetBValue(col) * 3 / 4);
        fb = CreateSolidBrush(col);
        FillRect(di->hDC, &rc, fb);
        FrameRect(di->hDC, &rc, bb);
        DeleteObject(fb);
        DeleteObject(bb);
        GetWindowTextA(di->hwndItem, t, sizeof t);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, RGB(0, 0, 0));
        of = SelectObject(di->hDC, GetStockObject(DEFAULT_GUI_FONT));
        if (pressed) OffsetRect(&rc, 1, 1);
        DrawTextA(di->hDC, t, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(di->hDC, of);
        return TRUE;
    }

    case WM_COMMAND:                /* clic sur un bouton : id = identifiant du bouton */
        id = LOWORD(wp);
        if (id == ID_STK) stk_menu(hw);
        else switch (id) {
            case ID_ADD:   key_char('+');  break;
            case ID_SUB:   key_char('-');  break;
            case ID_MUL:   key_char('*');  break;
            case ID_DIV:   key_char('/');  break;
        }
        refresh(hw);
        SetFocus(hw);               /* le clavier reste actif apres un clic */
        return 0;

    case WM_CHAR: {                 /* caractere tape : traite par key_char() ; fid = bouton a allumer */
        int fid = 0;
        switch ((int)wp) {
        case '+': fid = ID_ADD; break;
        case '-': fid = ID_SUB; break;
        case '*': case 'x': case 'X': fid = ID_MUL; break;
        case '/': fid = ID_DIV; break;
        }
        key_char((int)wp);
        if (fid) flash_btn(hw, fid);
        refresh(hw);
        return 0;
    }

    case WM_TIMER:                  /* fin de la surbrillance d'un bouton + - * / */
        if (wp == 1) {
            HWND b;
            KillTimer(hw, 1);
            if (flashId && (b = GetDlgItem(hw, flashId)) != NULL) {
                flashId = 0;
                InvalidateRect(b, NULL, FALSE);
            }
            flashId = 0;
        }
        return 0;

    case WM_KEYDOWN:                /* touches sans caractere : Suppr, fleches droite / gauche */
        if (wp == VK_DELETE) do_cmd("DROP");
        else if (wp == VK_RIGHT)  do_cmd("SWAP");
        else if (wp == VK_LEFT)   key_char('n');
        else break;
        refresh(hw);
        return 0;

    /* Clic gauche. Les coordonnees sont dans lp (x = mot bas, y = mot haut, valeurs signees).
     * Zones testees dans l'ordre : languette (zone elargie de 3 pixels), mot MENU, RAD/DEG. */
    case WM_LBUTTONDOWN:            /* clic sur la languette, sur MENU, ou sur RAD/DEG */
        if ((short)LOWORD(lp) >= rcStrip.left - 3) { toggle_ops(hw); SetFocus(hw); return 0; }
        if ((short)LOWORD(lp) >= rcDisp.left && (short)LOWORD(lp) < rcDisp.left + 70 &&
            (short)HIWORD(lp) >= rcDisp.top && (short)HIWORD(lp) < rcDisp.top + STATUS_H) {
            menu_dropdown(hw);
            refresh(hw);
            SetFocus(hw);
            return 0;
        }
        if (LOWORD(lp) > rcDisp.right - 60 && HIWORD(lp) < rcDisp.top + STATUS_H) {
            angDeg = !angDeg;
            refresh(hw);
        }
        SetFocus(hw);
        return 0;

    /* Double-clic (necessite CS_DBLCLKS dans la classe de fenetre). Sur la languette : meme
     * effet qu'un clic. Sur une ligne de pile : copie la valeur de ce niveau. */
    case WM_LBUTTONDBLCLK: {        /* double-clic sur une ligne : copie sa valeur */
        int x = LOWORD(lp), y = HIWORD(lp) - rcDisp.top;
        if (x >= rcStrip.left - 3) { toggle_ops(hw); SetFocus(hw); return 0; }
        if (x >= rcDisp.left && x < rcDisp.right &&
            y >= STATUS_H && y < STATUS_H + NLEVELS * LINE_H) {
            int lvl = NLEVELS - (y - STATUS_H) / LINE_H;
            if (lvl <= n && clip_set(st[n - lvl])) {
                char b[40];
                snprintf(b, sizeof b, "Niveau %d copi\xE9", lvl);
                setmsg(b);
                refresh(hw);
            }
        }
        SetFocus(hw);
        return 0;
    }

    case WM_RBUTTONUP:              /* clic droit : menu des options */
        context_menu(hw);
        refresh(hw);
        SetFocus(hw);
        return 0;

    case WM_SETCURSOR: {            /* main sur la languette */
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hw, &pt);
        if (LOWORD(lp) == HTCLIENT && pt.x >= rcStrip.left - 3) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_ACTIVATE:               /* fenetre reactivee : le clavier revient a la calculatrice */
        if (LOWORD(wp) != WA_INACTIVE) SetFocus(hw);
        return 0;

    case WM_PAINT:                  /* demande de redessin */
        paint(hw);
        return 0;

    case WM_DESTROY:                /* fermeture : sauvegarde de l'etat, liberation des polices */
        commit();
        save_state();
        DeleteObject(fMono);
        DeleteObject(fMenu);
        DeleteObject(fSup);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hw, msgid, wp, lp);     /* comportement standard pour tout le reste */
}

/* Point d'entree : relit l'etat, declare la classe de fenetre (icone numero 1 du fichier
 * .rc), cree la fenetre a la taille exacte voulue pour la zone cliente (AdjustWindowRect
 * ajoute bordure et barre de titre), puis boucle sur les messages jusqu'a la fermeture. */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    WNDCLASSEXA wc;
    RECT r = {0, 0, CW, CH};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    HWND hw;
    MSG m;
    (void)hp; (void)cmd;

    load_state();

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;           /* active WM_LBUTTONDBLCLK */
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hi, MAKEINTRESOURCEA(1));
    wc.hIconSm = (HICON)LoadImageA(hi, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "MiniCalcWnd";
    RegisterClassExA(&wc);

    AdjustWindowRect(&r, style, FALSE);
    hw = CreateWindowA("MiniCalcWnd", "MiniCalcRPN", style, CW_USEDEFAULT, CW_USEDEFAULT,
                       r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
    if (!hw) return 1;
    refresh(hw);
    if (topmost) set_topmost(hw);
    ShowWindow(hw, show);
    SetFocus(hw);

    while (GetMessage(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
#endif
