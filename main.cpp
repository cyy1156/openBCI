#include "mainwindow.h"
#include"thinkgear.h"
#include <QApplication>
#include <QDebug>
#include <iostream>
#include <limits>
#include <cstdlib>
#include <ctime>
#include <QDateTime>
void wait()
{
    std::cout<<"\n";
    std::cout<<"Press the ENTER key...\n"<<std::flush;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();

}
int main(int argc, char *argv[])
{
    /*QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();*/
    char* comPortName =NULL;
    int dllVersion =0;
    int connectionId=0;
    int packetsRead =0;
    int errCode=0;

    double secondsToRun=0;
    time_t startTime =0;
    time_t currTime =0;
    QString currTimeStr=NULL;
    int set_filter_falg=0;
    int count=0;

    dllVersion=TG_GetVersion();
    qDebug()<<"Stream SDK for PC version: "<<dllVersion;

    connectionId=TG_GetNewConnectionId();
    if(connectionId<0)
    {
        std::fprintf(stderr, "ERROR: TG_GetNewConnectionId() returned %d.\n", connectionId);
        std::cout << "Press Enter to exit..." << std::flush;
        wait();
        std::exit(EXIT_FAILURE);
    }

    comPortName="COM7";
    errCode=TG_Connect(
        connectionId,
        comPortName,
        TG_BAUD_57600,
        TG_STREAM_PACKETS
        );
    if(errCode!=0)
    {
        qDebug()<<"串口配置有错";
        return -1;
    }
    secondsToRun=5;
    startTime=time(nullptr);
    int rawport[513];

    while(difftime(time(nullptr),startTime)<secondsToRun)
    {

             packetsRead=TG_ReadPackets(connectionId,1);
            if(packetsRead==1)

             {
                 if (TG_GetValueStatus(connectionId, TG_DATA_POOR_SIGNAL) != 0) {
                    short signal = (short)TG_GetValue(connectionId, TG_DATA_POOR_SIGNAL);
                     qDebug()<<"信号质量"<<signal;
                }



                 if(TG_GetValueStatus(connectionId,TG_DATA_RAW)!=0)
                {
                    QDateTime now=QDateTime::currentDateTime();
                    currTimeStr= now.toString(Qt::TextDate);
                    double v=TG_GetValue(connectionId,TG_DATA_RAW);
                    short raw=(short)v;
                    //rawport[count++]=(int)raw;
                    count++;
                    qDebug()<<count<<"|"<<currTimeStr<<"|"<<"原始信号"<<raw;
                 }


                 if(TG_GetValueStatus(connectionId,TG_DATA_ATTENTION)!=0){

                    short ATT = (short)TG_GetValue(connectionId, TG_DATA_ATTENTION);
                     qDebug()<<"专注度Attention(值在0-100)"<<ATT;
                }
                 if (TG_GetValueStatus(connectionId, TG_DATA_MEDITATION) != 0) {
                     short med = (short)TG_GetValue(connectionId, TG_DATA_MEDITATION);
                     qDebug()<<"放松度Meditation值(0到100之间)"<<med;
                 }
                 if(count==513)
                 {
                     count=0;
                 }
            }



    }

    qDebug()<<"数据收集完成";
    TG_FreeConnection(connectionId);

    return 0;
}
