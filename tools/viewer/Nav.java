import java.awt.*;
import java.awt.image.*;
import java.awt.event.*;
import java.io.File;
import javax.swing.*;
import javax.swing.filechooser.*;

/** 
 * Two window nav + zoomed Viewer for PCL and PXL files.
 * 
 * Usage:
 * java Nav ../frs96.pxl
 * 
 * Adds a smaller navigation window coupled to a Gview window.
 * This allows a small portion of a page to be viewed at high resolution
 * in the Gview window with navigation occuring via the Nav window.
 * 
 * Mostly inherits behavior and reflects action on this window and the 
 * Gview zoomed window.
 *
 * @version $Revision$
 * @author Stefan Kemper
 */
public class Nav extends Gview  {

    public Nav() {
	pageView = new Gview();
    }

    protected void runMain(String[] args) {
	// NB no error checking.
	pickle.setJob(args[0]);
	origRes = desiredRes = startingRes / zoomWindowRatio;
	pickle.setRes(desiredRes, desiredRes);
	pageNumber = 1;
	pickle.setPageNumber(pageNumber);
	currentPage = pickle.getPrinterOutputPage();
	setSize(pickle.getImgWidth(), pickle.getImgHeight());
	origW = pickle.getImgWidth();
	origH = pickle.getImgHeight();
	show();

	pageView.runMain(args);
	repaint();
    }

    /** main program */
    public static void main( String[] args )
    {
	if (args.length < 1) {
	    System.out.println("Error: Missing input file\n" + usage());
	    System.exit(1);
	}
	System.out.print(usage());
	Nav view = new Nav();
        view.runMain(args);
    }

    public void nextPage() {
	super.nextPage();
	
	pageView.setPage(pageNumber);
    }

    public void prevPage() {
        super.prevPage();
	
	pageView.setPage(pageNumber);
    }

    /** low res image is ready, 
     * if we are not getting the next page 
     * start generation of the high res image 
     */
    public void imageIsReady( BufferedImage newImage ) {
	super.imageIsReady(newImage);

	if (pickle.busy() == false) {
	    pageView.pickle.startProduction(pageNumber);
	}
    }

    /** moves/drags zoomin box and causes regerenation of a new viewport
     */
    protected void translate(int x, int y) {
	origX -= x;
	origY -= y;
	
        double x1 = origX;
        double y1 = origY;

        double sfx = desiredRes / origRes;
        double sfy = desiredRes / origRes;

        double psfx = origX / origW * pageView.origW / pageView.origRes * pageView.desiredRes;
        double psfy = origY / origH * pageView.origH / pageView.origRes * pageView.desiredRes;
	
	pageView.translateTo( psfx, psfy );
	repaint();
    }

    /** Paint low res image with red zoom box
     *  zoom box uses xor realtime drag.
     */ 
    public void paint( Graphics g )
    {
	int h = (int)(origH * pageView.origRes / pageView.desiredRes);
	int w = (int)(origW * pageView.origRes / pageView.desiredRes);
	if (drag == true) {
	    g.setXORMode(Color.cyan);
	    g.drawRect((int)lastX, (int)lastY, w, h);
	    g.drawRect((int)newX, (int)newY, w, h);
	}
	else {
	    g.setPaintMode();	
	    g.drawImage(currentPage, 0, 0, this);
	    g.setColor(Color.red);  	
	    g.drawRect((int)origX, (int)origY, w, h);
	}
    }

    /** pageView gets regenerated at higher resolution,
     * repaint updates zoomin box.
     */
    protected void zoomIn( int x, int y ) {

        double psfx = x / origW * pageView.origW / pageView.origRes * pageView.desiredRes;
        double psfy = y / origH * pageView.origH / pageView.origRes * pageView.desiredRes;
	pageView.origX = pageView.origY = 0;
	pageView.zoomIn((int)psfx,(int)psfy);
	repaint();
    }

    /** pageView gets regenerated at lower resolution,
     * repaint updates zoomin box.
     */
    protected void zoomOut( int x, int y ) {
        double psfx = x / origW * pageView.origW / pageView.origRes * pageView.desiredRes;
        double psfy = y / origH * pageView.origH / pageView.origRes * pageView.desiredRes;
	pageView.origX = pageView.origY = 0;
	pageView.zoomOut(0,0);
	repaint();
    }

    /** pageView gets regenerated at requested resolution,
     * repaint updates zoomin box.
     */
    protected void zoomToRes( float res ) {		   
	pageView.zoomToRes(res);  
	repaint();
    }

    /**
     * High resolution window.
     * @link aggregationByValue
     */
    private Gview pageView;
}
