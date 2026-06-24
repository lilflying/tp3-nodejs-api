package com.efrei.employees.config;

import com.efrei.employees.model.Employee;
import com.efrei.employees.repository.EmployeeRepository;
import org.springframework.boot.CommandLineRunner;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class DataSeeder {

    @Bean
    CommandLineRunner seed(EmployeeRepository repository) {
        return args -> {
            if (repository.count() == 0) {
                repository.save(new Employee("Alice", "Developer", 4500.0));
                repository.save(new Employee("Bob", "Developer", 5500.0));
                repository.save(new Employee("Sophie", "Manager", 6000.0));
                repository.save(new Employee("Pierre", "Developer", 4800.0));
            }
        };
    }
}
