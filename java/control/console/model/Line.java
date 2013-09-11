package forest.control.console.model;

import forest.common.RateSpec;

public class Line{
    private int lx, ly, rx, ry = 0;
    private boolean strong = false;
    private RateSpec rs;
    private RateSpec availableRs;
//    private RateSpec ctRs;
    public Line(int lx, int ly, int rx, int ry, boolean strong, 
    		RateSpec rs, RateSpec availableRs){
        this.lx = lx; this.ly = ly;
        this.rx = rx; this.ry = ry;
        this.strong = strong;
        this.rs = rs; //this.ctRs = ctRs;
        this.availableRs = availableRs;
    }
    public int getLx(){ return lx;}
    public int getLy(){ return ly;}
    public int getRx(){ return rx;}
    public int getRy(){ return ry;}
    public boolean isStrong(){return strong;}
    public RateSpec getRateSpec(){return rs;}
    public RateSpec getAvailableRS(){return availableRs;}
//    public RateSpec getCtRs(){return ctRs;}
}
