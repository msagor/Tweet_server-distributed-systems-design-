/* Standard C++ includes */
#include <stdlib.h>
#include <string.h>
#include <iostream>

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>

using namespace std;

int main(void)
{
    try {
        sql::Driver *driver;
        sql::Connection *con;
        sql::PreparedStatement* prep_stmt;
        sql::ResultSet* res;

        /* Create a connection */
        driver = get_driver_instance();
        con = driver->connect("localhost", "root", "root");
        /* Connect to the MySQL test database */

        prep_stmt = con->prepareStatement("CREATE DATABASE IF NOT EXISTS test");
        prep_stmt->executeUpdate();
        delete prep_stmt;
        con->setSchema("test");
        prep_stmt = con->prepareStatement("DROP TABLE Faults");
        prep_stmt->executeUpdate();
        delete prep_stmt;
        prep_stmt = con->prepareStatement("CREATE TABLE IF NOT EXISTS Faults(id INTEGER AUTO_INCREMENT PRIMARY KEY, location CHAR(255) NOT NULL)");
        prep_stmt->executeUpdate();
        delete prep_stmt;
        prep_stmt = con->prepareStatement("INSERT INTO Faults(location) VALUES(?)");

        for (int i = 0; i < 1000; ++i)
        {
            prep_stmt->setString(1, "here");
            prep_stmt->executeUpdate();
        }
        delete prep_stmt;

        prep_stmt = con->prepareStatement("SELECT * From Faults");
        res = prep_stmt->executeQuery();
        while (res->next()) {
            cout << "\t... MySQL replies: ";
            /* Access column data by alias or column name */
            cout << res->getString("Location") << endl;
          }

        delete prep_stmt;
        delete res;
        delete con;

    } catch (sql::SQLException& e) {
        cerr << "# ERR: SQLException in " << __FILE__;
        cerr << "(" << __FUNCTION__ << ") on line " << __LINE__ << endl;
        cerr << "# ERR: " << e.what();
        cerr << " (MySQL error code: " << e.getErrorCode();
        cerr << ", SQLState: " << e.getSQLState() << " )" << endl;
    }
    catch(std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    cout << endl;

    return EXIT_SUCCESS;
}
