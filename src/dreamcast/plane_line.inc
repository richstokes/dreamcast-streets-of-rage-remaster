// Native CPU fallback: invariant tile decode once per run, not once per pixel.
// Preserves per-two-cell vertical scroll boundaries even when horizontal scroll
// makes those boundaries fall in the middle of a tile.
void VDPRenderer::buildPlaneLine(PixelResult *destination, int planeBase,
    int hscroll, int baseVscroll, int vsramOffset, bool twoCellVscroll,
    int line, int activeWidth, int planeWidthCells, int planeHeightCells,
    bool interlace2) const {
    const auto &s=state_;
    const int xMask=planeWidthCells*8-1, yMask=planeHeightCells*8-1;
    int planeX=(-hscroll)&xMask;
    for(int x=0;x<activeWidth;){
        int vscroll=baseVscroll;
        if(twoCellVscroll){int i=(x>>4)*2+vsramOffset;if(i<VDPState::VSRAM_ENTRIES)vscroll=static_cast<int16_t>(s.vsram_[i]);}
        int py=(line+vscroll)&yMask, pixelY=py&(interlace2?15:7);
        int addr=(planeBase+((py>>(interlace2?4:3))*planeWidthCells+(planeX>>3))*2)&65535;
        unsigned entry=(unsigned(s.vram_[addr])<<8)|s.vram_[(addr+1)&65535];
        int tile=entry&2047;
        if(interlace2){tile=((entry&1023)<<1)|(pixelY>>3);pixelY&=7;}
        if(entry&4096)pixelY=7-pixelY;
        int row=tile*32+pixelY*4;
        int px=planeX&7,n=std::min(8-px,activeWidth-x);
        if(twoCellVscroll)n=std::min(n,16-(x&15));
        uint32_t flags=((entry>>13)&3)<<8 | ((entry>>15)&1)<<16;
        for(int i=0;i<n;i++){
            int tx=(entry&2048)?7-(px+i):px+i;
            unsigned packed=s.vram_[row+(tx>>1)];
            unsigned color=(tx&1)?packed&15:packed>>4;
            // Both supported execution targets are little-endian; bool bytes are 0/1.
            uint32_t value=flags|color|(uint32_t(color!=0)<<24);
            static_assert(sizeof(PixelResult)==4);
            std::memcpy(destination+x+i,&value,4);
        }
        x+=n;planeX=(planeX+n)&xMask;
    }
}
