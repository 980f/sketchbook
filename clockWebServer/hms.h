class HMS {
  public:
    int hour;
    int minute;
    int sec;
    int mils;

    HMS(long tick);
    void setTimeFrom(const char *value) ;
};
