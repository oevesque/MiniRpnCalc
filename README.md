# MiniCalc RPN

Calculatrice scientifique à **notation polonaise inverse** (RPN, comme les HP) pour Windows.
Un seul petit exécutable (environ 100 Ko), sans installation ni dépendance, qui **démarre instantanément**.

![Fenêtre principale de MiniCalc RPN](docs/capture-principale.png)

## Pourquoi ce programme ?

MiniCalc RPN est la réécriture, en C pur avec l'API Win32, d'une petite calculatrice RPN écrite en Visual Basic il y a plus de 25 ans et dont les sources étaient perdues. L'ancienne version avait un défaut gênant : sur un ordinateur portable sans pavé numérique, **le chiffre 8 était impossible à saisir**. Le programme lit désormais le caractère réellement produit par la touche, quelle que soit la disposition du clavier (AZERTY, QWERTY…), ce qui règle le problème.

La réécriture a gardé l'esprit de l'original (écran de pile, menus de fonctions, démarrage éclair) et y ajoute quelques commodités : boutons `+ - × ÷` pour les claviers sans pavé numérique, copie d'une valeur par double-clic, mémorisation de la pile entre deux sessions…

## Fonctionnalités

- Pile RPN avec **6 niveaux affichés** (jusqu'à 512 niveaux en mémoire).
- Opérations de base, puissances, racine, inverse, logarithmes, exponentielles, π.
- Trigonométrie directe et inverse, en **degrés (par défaut) ou en radians**, avec conversions.
- Pourcentages, combinaisons *Cnp*, factorielle, résolution des équations du second degré.
- Manipulation de la pile : `SWAP`, `DUP2`, `OVER`, `ROT`, `AROT`, `PICK`, `DEPTH`.
- **Double-clic sur une ligne de la pile** : copie sa valeur dans le presse-papiers.
- Boutons `÷ × - +` **repliables** (languette sur le bord droit) : utiles sans pavé numérique, masqués sinon. Au clavier, le bouton correspondant s'allume brièvement.
- La pile, les réglages et l'état de la languette sont **conservés d'une session à l'autre**.
- Option « toujours au premier plan ».

## Captures d'écran

| Liste des fonctions (clic sur **MENU**) | Boutons dépliés et liste **STK** |
|---|---|
| ![Liste MENU en 5 colonnes](docs/capture-menu.png) | ![Boutons + - × ÷ et liste STK](docs/capture-boutons.png) |

## Utilisation

### Le principe de la RPN

On saisit d'abord les nombres, puis l'opération : pas de touche « = » ni de parenthèses.
Pour calculer `(2 + 3) × 4` :

```
2  Entrée  3  +  4  ×      →  20
```

Valider la saisie avec Entrée n'est nécessaire que pour séparer deux nombres : taper une opération ou choisir une fonction valide automatiquement le nombre en cours de saisie. Le nombre le plus récent est au **niveau 1** (en bas de l'écran), le précédent au niveau 2, etc.

### Clavier

| Touche | Action |
|---|---|
| `0-9`, `.` ou `,` | Saisie d'un nombre |
| `Entrée` | Valide la saisie, ou duplique le niveau 1 si rien n'est saisi |
| `+` `-` `*` `/` | Opérations (la lettre `x` fait aussi la multiplication) |
| `^` | x puissance y |
| `n` ou `←` | Change le signe (saisie en cours, sinon niveau 1) |
| `i` | Inverse (1/x) |
| `d` | Duplique les niveaux 1 et 2 (`DUP2`) |
| `s` ou `→` | Échange les niveaux 1 et 2 (`SWAP`) |
| `Retour arrière` | Efface un caractère, ou supprime le niveau 1 si rien n'est saisi |
| `Suppr` | Supprime le niveau 1 (`DROP`) |
| `Échap` | Annule la saisie en cours |
| `c` ou `Ctrl+C` | Copie le niveau 1 dans le presse-papiers |
| `p` ou `Ctrl+V` | Colle un nombre depuis le presse-papiers |
| `m` | Bascule degrés / radians (un clic sur « DEG » ou « RAD » aussi) |

### Liste des fonctions (clic sur **MENU**, en haut à gauche)

| Colonne 1 | Colonne 2 | Colonne 3 | Colonne 4 | Colonne 5 |
|---|---|---|---|---|
| ± (change le signe) | xʸ | sin | x%y | Cₙᵖ |
| 1/x | eˣ | cos | d% | ! |
| √ | ln | tan | x-y% | ax²+bx+c |
| x² | 10ˣ | *arc :* asin, acos, atan | x+y% | |
| π | Log | DEG->RAD, RAD->DEG | | |

Quelques précisions (*y* = niveau 2, *x* = niveau 1) :

| Fonction | Effet | Exemple |
|---|---|---|
| `x%y` | x % de y (y est conservé) | `100` `20` → 20 |
| `d%` | variation en % de y vers x | `100` `120` → 20 |
| `x+y%` / `x-y%` | y augmenté / diminué de x % | `200` `15` `x+y%` → 230 |
| `Cₙᵖ` | combinaisons de p parmi n (n au niveau 2, p au niveau 1) | `49` `6` → 13 983 816 |
| `!` | factorielle (entier de 0 à 170) | `5` → 120 |
| `ax²+bx+c` | racines : a au niveau 3, b au niveau 2, c au niveau 1 | `1` `-3` `2` → 2 et 1 |
| `DEG->RAD`, `RAD->DEG` | conversion d'un angle | `180` → 3,14159… |

Quand l'équation du second degré n'a pas de racine réelle, le résultat est la partie réelle puis la partie imaginaire, avec le message « complexe re,im ».

### Bouton STK

Ouvre la liste des fonctions de pile : `SWAP`, `DUP2`, `OVER` (copie le niveau 2 au sommet), `ROT` (le niveau 3 passe au niveau 1), `AROT` (le niveau 1 passe au niveau 3), `PICK` (copie le niveau indiqué par le niveau 1) et `DEPTH` (empile le nombre de niveaux).

### Clic droit

Copier le sommet, coller, toujours au premier plan, afficher les boutons `+ - × ÷`, effacer la pile, passer en degrés ou en radians, aide, à propos.

## Installation

Aucune installation : copiez `MiniCalc.exe` où vous voulez et lancez-le. Comme l'exécutable n'est pas signé, Windows (SmartScreen) ou un antivirus peut afficher un avertissement au premier lancement.

L'état est sauvegardé à la fermeture dans `%APPDATA%\MiniCalc\MiniCalc.dat` ; supprimez ce fichier pour repartir de zéro.

## Compilation

Fichiers nécessaires, dans le même dossier : `minicalc.c`, `minicalc.rc`, `minicalc.ico`.

**Avec Visual Studio ou les Build Tools C++ (le plus simple)** : double-cliquez sur `build.bat`. Le script trouve tout seul le compilateur de Microsoft (ou, à défaut, MinGW) et produit `MiniCalc.exe`, icône comprise. Depuis VS Code, la configuration `.vscode/tasks.json` fournie permet de lancer la même génération avec `Ctrl+Maj+B`.

**Avec MinGW** :

```
i686-w64-mingw32-windres minicalc.rc -O coff -o minicalc_res.o
i686-w64-mingw32-gcc -Os -s -static -mwindows minicalc.c minicalc_res.o -o MiniCalc.exe -lgdi32 -luser32 -lm
```

**Test du moteur de calcul seul** (sans fenêtre, sous Linux ou avec MinGW) :

```
gcc -DTEST minicalc.c -o t -lm
./t "2@3@" "+"          # @ = Entrée, "!SWAP" = commande
```

## Pour les développeurs

Tout le programme tient dans `minicalc.c`, abondamment commenté. L'en-tête du fichier explique l'organisation (moteur RPN, clavier, interface Windows) et la marche à suivre pour **ajouter une fonction** : l'écrire dans `do_cmd()`, puis l'ajouter au tableau `mitems[]` (liste MENU) ou au menu du bouton STK.

## Crédits

Calculatrice RPN d'origine : petit programme Visual Basic (« Mini RPN Calc »), dont les sources n'ont pas été conservées.
Réécriture en C : « vibecodée » avec Claude Sonnet 5.5 (Anthropic).
