# GTR_Framework - Final Assignment
OpenGL C++ Framework used for the Realistic Realtime Rendering course at Universitat Pompeu Fabra.

#Alvar Alonso Rovira NIA: 192880 - alvar.alonso01@estudiant.upf.edu
#Pau Florenti Solano NIA: 193968 - pau.florenti01@estudian.upf.edu

Date: 28/6/2020

Trabajo final de la asignatura GTR de la Universidad Pompeu Fabra. En esta última entrega se ha aplicado lo siguiente:

 En el codigo hay la opción de generar dos escenas. En la primera tenemos varias entidades de coches y luces con diferentes propiedades.

* # SSAO+:
  Para la ambient occlusion se ha generado una textura utilizando los hemisferios orientados según la normal para mejorar su calidad. A la textura se le aplica un blur sencillo para suavizar los bordes de las sombras.

 * # IRRADIANCIA:
    Para la irradiancia se han generado las probes, visibles con una opción añadida en el GUI de la aplicación. También se ha realizado una interpolación trilineal entre las probes para suavizar el salto entre zonas afectadas por las probes. Con el botón "i" se calcula y se sube la textura con los valores a disco. Se carga con el botón "l". En caso de tener el fichero ya generado no hace falta cargarlo, en la carpeta viene añadido el de la escena 2.
    
* # REFLECTIONS:
  Para el apartado de reflexiones se ha cargado un fondo HDR en un skybox. Este se puede ver reflejado en los objetos metalicos que dispongan de dicha propiedad. También hemos implementado reflexión por probe. Hay en el GUI de la aplicación la opción de activar/desactivar las reflexiones y también de mostrar por pantalla las probes mencionadas.
  
* # VOLUMETRIC LIGHT:
  La luz volumetrica se ha implementado sólo para la luz direccional. Esta opción también se puede activar o desactivar des de una opción del GUI.
  
* # DECALS
 Para terminar hemos implementado la opción de poder añadir Decals en nuestro render. Se puede activar y ver uno implementado en la escena.
