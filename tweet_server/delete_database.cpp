#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>

int main()
{    
    try
    {
        sql::Driver* driver;
        sql::Connection* con;
        sql::PreparedStatement* prep_stmt;
        sql::ResultSet* res;
        driver = get_driver_instance();
        con = driver->connect("localhost:3306", "root", "root");
        con->setSchema("tinysns");

        // get the name of each user that username is following
        prep_stmt = con->prepareStatement("DROP DATABASE tinysns;");
        res = prep_stmt->executeQuery();

        delete prep_stmt;
        delete res;
        delete con;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "# ERR: SQLException in " << __FILE__;
        std::cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << std::endl;
        std::cerr << "# ERR: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl << "on line: " << __LINE__ << std::endl;
    }
    return 0;
}
